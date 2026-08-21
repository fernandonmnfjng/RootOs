#include "driver_store.h"
#include "driver_api.h"
#include "elf_module.h"
#include "device_manager.h"
#include "heap.h"
#include "memory.h"
#include "string.h"
#include <rootos/rootdriver.h>

#define MULTIBOOT_BOOTLOADER_MAGIC_LOCAL 0x2BADB002u
#define MULTIBOOT_INFO_MODS (1u << 3)
#define RDP_VERSION 1u
#define RDP_COMPRESSION_NONE 0u
#define RDP_COMPRESSION_RLE8 1u
#define RDP_ENTRY_SIZE 180u
#define RDP_HEADER_SIZE 32u
#define RDP_MAGIC "RDP10001"

typedef struct __attribute__((packed))
{
    u32 flags; u32 mem_lower; u32 mem_upper; u32 boot_device; u32 cmdline;
    u32 mods_count; u32 mods_addr;
} MultibootInfoPrefix;

typedef struct __attribute__((packed))
{
    u32 mod_start; u32 mod_end; u32 string; u32 reserved;
} MultibootModule;

typedef struct __attribute__((packed))
{
    char magic[8];
    u32 version;
    u32 entry_count;
    u32 index_offset;
    u32 index_size;
    u32 data_offset;
    u32 total_size;
} RdpHeader;

typedef struct __attribute__((packed))
{
    u16 vendor_id; u16 device_id; u8 class_code; u8 subclass; u8 prog_if; u8 reserved;
} RdpMatch;

typedef struct __attribute__((packed))
{
    char name[32];
    u32 module_offset;
    u32 compressed_size;
    u32 uncompressed_size;
    u32 crc32;
    u8 compression;
    u8 match_count;
    u8 bus_type;
    u8 reserved;
    RdpMatch matches[8];
    char description[64];
} RdpEntry;

typedef int (*RootDriverEntryFn)(const RootDriverApi*, const RootDriverDeviceInfo*);

typedef struct
{
    bool used;
    u32 entry_index;
    u32 device_id;
    RootLoadedModule module;
} LoadedRecord;

static const u8* pack = NULL;
static usize pack_size = 0u;
static const RdpHeader* header = NULL;
static const RdpEntry* entries = NULL;
static LoadedRecord loaded[ROOT_DRIVERPACK_LOADED_MAX];
static usize loaded_count = 0u;
static const char* last_error = "driver pack not found";

static bool fixed_string_ok(const char* text, usize size)
{
    for (usize i = 0u; i < size; i++) if (text[i] == '\0') return i != 0u;
    return false;
}

static u32 crc32_bytes(const u8* data, usize size)
{
    u32 crc = 0xFFFFFFFFu;
    for (usize i = 0u; i < size; i++)
    {
        crc ^= data[i];
        for (u32 b = 0u; b < 8u; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static bool rle8_decode(const u8* input, usize input_size, u8* output, usize output_size)
{
    usize in = 0u, out = 0u;
    while (in < input_size && out < output_size)
    {
        u8 control = input[in++];
        if (control < 128u)
        {
            usize length = (usize)control + 1u;
            if (length > input_size - in || length > output_size - out) return false;
            root_memcpy(output + out, input + in, length);
            in += length; out += length;
        }
        else
        {
            usize length = (usize)(control & 0x7Fu) + 3u;
            if (in >= input_size || length > output_size - out) return false;
            u8 value = input[in++];
            for (usize i = 0u; i < length; i++) output[out++] = value;
        }
    }
    return in == input_size && out == output_size;
}

static bool entry_matches(const RdpEntry* entry, const RootDevice* device)
{
    if (entry == NULL || device == NULL || device->bus != ROOT_DEVICE_BUS_PCI || entry->bus_type != ROOT_DRIVER_BUS_PCI)
        return false;
    u8 count = entry->match_count;
    if (count > 8u) count = 8u;
    for (u8 i = 0u; i < count; i++)
    {
        const RdpMatch* m = &entry->matches[i];
        if (m->vendor_id != 0xFFFFu && m->vendor_id != device->pci.vendor_id) continue;
        if (m->device_id != 0xFFFFu && m->device_id != device->pci.device_id) continue;
        if (m->class_code != 0xFFu && m->class_code != device->pci.class_code) continue;
        if (m->subclass != 0xFFu && m->subclass != device->pci.subclass) continue;
        if (m->prog_if != 0xFFu && m->prog_if != device->pci.prog_if) continue;
        return true;
    }
    return false;
}

static bool entry_loaded(u32 index)
{
    for (usize i = 0u; i < loaded_count; i++) if (loaded[i].used && loaded[i].entry_index == index) return true;
    return false;
}

static bool validate_pack(void)
{
    if (pack == NULL || pack_size < sizeof(RdpHeader)) return false;
    header = (const RdpHeader*)pack;
    if (root_memcmp(header->magic, RDP_MAGIC, 8u) != 0 || header->version != RDP_VERSION || header->total_size > pack_size)
        return false;
    if (header->entry_count > 64u || header->index_offset < RDP_HEADER_SIZE || header->index_size != header->entry_count * RDP_ENTRY_SIZE)
        return false;
    if (header->index_offset > pack_size || header->index_size > pack_size - header->index_offset)
        return false;
    entries = (const RdpEntry*)(pack + header->index_offset);
    for (u32 i = 0u; i < header->entry_count; i++)
    {
        const RdpEntry* e = &entries[i];
        if (!fixed_string_ok(e->name, sizeof(e->name)) || e->match_count > 8u || e->module_offset > pack_size || e->compressed_size > pack_size - e->module_offset)
            return false;
    }
    return true;
}

void driver_store_init(u32 multiboot_magic, u32 multiboot_info_address)
{
    pack = NULL; pack_size = 0u; header = NULL; entries = NULL; loaded_count = 0u;
    root_memzero(loaded, sizeof(loaded));
    last_error = "driver pack not found";
    driver_api_init();

    if (multiboot_magic != MULTIBOOT_BOOTLOADER_MAGIC_LOCAL || multiboot_info_address == 0u)
    { last_error = "invalid Multiboot info"; return; }

    const MultibootInfoPrefix* info = (const MultibootInfoPrefix*)(usize)multiboot_info_address;
    if ((info->flags & MULTIBOOT_INFO_MODS) == 0u || info->mods_count == 0u || info->mods_addr == 0u)
    { last_error = "GRUB did not load driverpack.rdp"; return; }

    const MultibootModule* mods = (const MultibootModule*)(usize)info->mods_addr;
    for (u32 i = 0u; i < info->mods_count; i++)
    {
        if (mods[i].mod_end <= mods[i].mod_start) continue;
        const char* command = mods[i].string != 0u ? (const char*)(usize)mods[i].string : NULL;
        if (command != NULL && root_strncmp(command, "driverpack", 10u) != 0) continue;
        pack = (const u8*)(usize)mods[i].mod_start;
        pack_size = (usize)(mods[i].mod_end - mods[i].mod_start);
        if (validate_pack()) { last_error = "none"; return; }
        pack = NULL; pack_size = 0u; header = NULL; entries = NULL;
        last_error = "driver pack format invalid";
    }
}

static bool load_for_device(u32 entry_index, RootDevice* device)
{
    if (entry_index >= header->entry_count || device == NULL || loaded_count >= ROOT_DRIVERPACK_LOADED_MAX)
        return false;
    const RdpEntry* e = &entries[entry_index];
    const u8* packed_module = pack + e->module_offset;
    u8* raw = (u8*)root_malloc(e->uncompressed_size);
    if (raw == NULL) { last_error = "driver decompression allocation failed"; return false; }

    bool decoded = false;
    if (e->compression == RDP_COMPRESSION_NONE && e->compressed_size == e->uncompressed_size)
    { root_memcpy(raw, packed_module, e->uncompressed_size); decoded = true; }
    else if (e->compression == RDP_COMPRESSION_RLE8)
    { decoded = rle8_decode(packed_module, e->compressed_size, raw, e->uncompressed_size); }
    if (!decoded) { root_free(raw); last_error = "driver decompression failed"; return false; }
    if (crc32_bytes(raw, e->uncompressed_size) != e->crc32)
    { root_free(raw); last_error = "driver CRC mismatch"; return false; }

    RootLoadedModule module;
    RootModuleResult result = elf_module_load_memory(raw, e->uncompressed_size, "root_driver_entry", &module);
    root_free(raw);
    if (result != ROOT_MODULE_OK) { last_error = elf_module_result_string(result); return false; }

    RootDriverDeviceInfo info;
    driver_api_make_device(device, &info);
    RootDriverEntryFn entry = (RootDriverEntryFn)module.entry;
    int driver_result = entry(driver_api(), &info);
    if (driver_result != ROOT_DRIVER_OK)
    { root_free(module.image_allocation); last_error = "driver entry returned failure"; return false; }

    LoadedRecord* record = &loaded[loaded_count++];
    record->used = true; record->entry_index = entry_index; record->device_id = device->id;
    root_memcpy(&record->module, &module, sizeof(module));
    (void)device_manager_mark_external_bound(device->id, e->name);
    last_error = "none";
    return true;
}

void driver_store_bind_all(void)
{
    if (!driver_store_available()) return;
    for (usize d = 0u; d < device_manager_count(); d++)
    {
        RootDevice device;
        if (!device_manager_get(d, &device) || device.driver_state == ROOT_DEVICE_DRIVER_BOUND) continue;
        for (u32 e = 0u; e < header->entry_count; e++)
        {
            if (!entry_matches(&entries[e], &device)) continue;
            if (load_for_device(e, &device)) break;
            (void)device_manager_mark_external_failed(device.id, entries[e].name);
            break;
        }
    }
}

bool driver_store_available(void) { return header != NULL && entries != NULL; }
usize driver_store_entry_count(void) { return header != NULL ? header->entry_count : 0u; }
usize driver_store_loaded_count(void) { return loaded_count; }
const char* driver_store_last_error(void) { return last_error; }

bool driver_store_get(usize index, RootDriverPackEntryInfo* output)
{
    if (output == NULL || header == NULL || index >= header->entry_count) return false;
    const RdpEntry* e = &entries[index];
    root_memzero(output, sizeof(*output));
    root_strlcpy(output->name, e->name, sizeof(output->name));
    root_strlcpy(output->description, e->description, sizeof(output->description));
    output->compression = e->compression;
    output->match_count = e->match_count;
    output->compressed_size = e->compressed_size;
    output->uncompressed_size = e->uncompressed_size;
    output->loaded = entry_loaded((u32)index);
    for (u8 i = 0u; i < e->match_count && i < ROOT_DRIVERPACK_MATCH_MAX; i++)
    {
        output->matches[i].vendor_id = e->matches[i].vendor_id;
        output->matches[i].device_id = e->matches[i].device_id;
        output->matches[i].class_code = e->matches[i].class_code;
        output->matches[i].subclass = e->matches[i].subclass;
        output->matches[i].prog_if = e->matches[i].prog_if;
    }
    return true;
}
