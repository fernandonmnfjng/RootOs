#include "elf_module.h"
#include "heap.h"
#include "memory.h"
#include "string.h"

#define EI_NIDENT 16u
#define ELFCLASS32 1u
#define ELFDATA2LSB 1u
#define EV_CURRENT 1u
#define ET_REL 1u
#define EM_386 3u
#define SHT_PROGBITS 1u
#define SHT_SYMTAB 2u
#define SHT_STRTAB 3u
#define SHT_RELA 4u
#define SHT_NOBITS 8u
#define SHT_REL 9u
#define SHF_ALLOC 0x2u
#define SHN_UNDEF 0u
#define SHN_ABS 0xFFF1u
#define SHN_COMMON 0xFFF2u
#define R_386_NONE 0u
#define R_386_32 1u
#define R_386_PC32 2u
#define ROOT_MODULE_MAX_SECTIONS 96u
#define ROOT_MODULE_MAX_IMAGE (2u * 1024u * 1024u)
#define ROOT_MODULE_MAX_ALIGN 4096u
#define ELF32_R_SYM(info) ((info) >> 8)
#define ELF32_R_TYPE(info) ((u8)(info))

typedef struct __attribute__((packed))
{
    u8 e_ident[EI_NIDENT]; u16 e_type; u16 e_machine; u32 e_version;
    u32 e_entry; u32 e_phoff; u32 e_shoff; u32 e_flags; u16 e_ehsize;
    u16 e_phentsize; u16 e_phnum; u16 e_shentsize; u16 e_shnum; u16 e_shstrndx;
} Elf32Ehdr;

typedef struct __attribute__((packed))
{
    u32 sh_name; u32 sh_type; u32 sh_flags; u32 sh_addr; u32 sh_offset;
    u32 sh_size; u32 sh_link; u32 sh_info; u32 sh_addralign; u32 sh_entsize;
} Elf32Shdr;

typedef struct __attribute__((packed))
{
    u32 st_name; u32 st_value; u32 st_size; u8 st_info; u8 st_other; u16 st_shndx;
} Elf32Sym;

typedef struct __attribute__((packed)) { u32 r_offset; u32 r_info; } Elf32Rel;

static bool range_ok(usize offset, usize amount, usize total)
{
    return offset <= total && amount <= total - offset;
}

static bool pow2(usize value)
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

static usize align_up(usize value, usize alignment)
{
    if (alignment <= 1u) return value;
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static const char* table_string(const u8* table, usize size, u32 offset)
{
    if (table == NULL || offset >= size) return NULL;
    for (usize i = offset; i < size; i++)
        if (table[i] == 0u) return (const char*)(table + offset);
    return NULL;
}

static bool symbol_address(
    const Elf32Sym* symbol,
    const Elf32Shdr* sections,
    u16 section_count,
    u8* const* runtime,
    u32* address
)
{
    if (symbol == NULL || address == NULL) return false;
    if (symbol->st_shndx == SHN_ABS) { *address = symbol->st_value; return true; }
    if (
        symbol->st_shndx == SHN_UNDEF || symbol->st_shndx == SHN_COMMON ||
        symbol->st_shndx >= section_count || runtime[symbol->st_shndx] == NULL ||
        symbol->st_value > sections[symbol->st_shndx].sh_size
    ) return false;
    *address = (u32)(usize)runtime[symbol->st_shndx] + symbol->st_value;
    return true;
}

RootModuleResult elf_module_load_memory(
    const void* source,
    usize file_size,
    const char* entry_symbol,
    RootLoadedModule* output
)
{
    if (output != NULL) root_memzero(output, sizeof(*output));
    if (source == NULL || output == NULL || entry_symbol == NULL || file_size < sizeof(Elf32Ehdr))
        return ROOT_MODULE_BAD_FORMAT;

    const u8* file = (const u8*)source;
    const Elf32Ehdr* header = (const Elf32Ehdr*)file;
    if (
        header->e_ident[0] != 0x7Fu || header->e_ident[1] != 'E' ||
        header->e_ident[2] != 'L' || header->e_ident[3] != 'F' ||
        header->e_ident[4] != ELFCLASS32 || header->e_ident[5] != ELFDATA2LSB ||
        header->e_ident[6] != EV_CURRENT || header->e_type != ET_REL ||
        header->e_machine != EM_386 || header->e_version != EV_CURRENT ||
        header->e_ehsize != sizeof(Elf32Ehdr) || header->e_shentsize != sizeof(Elf32Shdr) ||
        header->e_shnum == 0u || header->e_shnum > ROOT_MODULE_MAX_SECTIONS ||
        !range_ok(header->e_shoff, (usize)header->e_shnum * sizeof(Elf32Shdr), file_size)
    ) return ROOT_MODULE_BAD_FORMAT;

    const Elf32Shdr* sections = (const Elf32Shdr*)(file + header->e_shoff);
    usize offsets[ROOT_MODULE_MAX_SECTIONS];
    u8* runtime[ROOT_MODULE_MAX_SECTIONS];
    root_memzero(offsets, sizeof(offsets));
    root_memzero(runtime, sizeof(runtime));

    usize image_size = 0u;
    usize max_align = 16u;
    for (u16 i = 0u; i < header->e_shnum; i++)
    {
        const Elf32Shdr* s = &sections[i];
        if ((s->sh_flags & SHF_ALLOC) == 0u) continue;
        usize a = s->sh_addralign == 0u ? 1u : s->sh_addralign;
        if (!pow2(a) || a > ROOT_MODULE_MAX_ALIGN) return ROOT_MODULE_UNSUPPORTED;
        if (a > max_align) max_align = a;
        image_size = align_up(image_size, a);
        offsets[i] = image_size;
        if (s->sh_size > ROOT_MODULE_MAX_IMAGE || image_size > ROOT_MODULE_MAX_IMAGE - s->sh_size)
            return ROOT_MODULE_UNSUPPORTED;
        image_size += s->sh_size;
        if (s->sh_type != SHT_NOBITS && !range_ok(s->sh_offset, s->sh_size, file_size))
            return ROOT_MODULE_BAD_FORMAT;
    }
    if (image_size == 0u) return ROOT_MODULE_BAD_FORMAT;

    u8* allocation = (u8*)root_malloc(image_size + max_align);
    if (allocation == NULL) return ROOT_MODULE_NO_MEMORY;
    u8* image = (u8*)align_up((usize)allocation, max_align);
    root_memzero(image, image_size);

    for (u16 i = 0u; i < header->e_shnum; i++)
    {
        const Elf32Shdr* s = &sections[i];
        if ((s->sh_flags & SHF_ALLOC) == 0u) continue;
        runtime[i] = image + offsets[i];
        if (s->sh_type != SHT_NOBITS && s->sh_size != 0u)
            root_memcpy(runtime[i], file + s->sh_offset, s->sh_size);
    }

    const Elf32Sym* symbols = NULL;
    usize symbol_count = 0u;
    const u8* strings = NULL;
    usize strings_size = 0u;
    for (u16 i = 0u; i < header->e_shnum; i++)
    {
        const Elf32Shdr* s = &sections[i];
        if (s->sh_type != SHT_SYMTAB) continue;
        if (
            s->sh_entsize != sizeof(Elf32Sym) || s->sh_link >= header->e_shnum ||
            sections[s->sh_link].sh_type != SHT_STRTAB ||
            !range_ok(s->sh_offset, s->sh_size, file_size) ||
            !range_ok(sections[s->sh_link].sh_offset, sections[s->sh_link].sh_size, file_size)
        ) { root_free(allocation); return ROOT_MODULE_BAD_FORMAT; }
        symbols = (const Elf32Sym*)(file + s->sh_offset);
        symbol_count = s->sh_size / sizeof(Elf32Sym);
        strings = file + sections[s->sh_link].sh_offset;
        strings_size = sections[s->sh_link].sh_size;
        break;
    }
    if (symbols == NULL || symbol_count == 0u) { root_free(allocation); return ROOT_MODULE_NO_ENTRY; }

    for (u16 i = 0u; i < header->e_shnum; i++)
    {
        const Elf32Shdr* rels = &sections[i];
        if (rels->sh_type == SHT_RELA) { root_free(allocation); return ROOT_MODULE_UNSUPPORTED; }
        if (rels->sh_type != SHT_REL) continue;
        if (
            rels->sh_entsize != sizeof(Elf32Rel) || rels->sh_info >= header->e_shnum ||
            rels->sh_link >= header->e_shnum || sections[rels->sh_link].sh_type != SHT_SYMTAB ||
            !range_ok(rels->sh_offset, rels->sh_size, file_size) || runtime[rels->sh_info] == NULL
        ) { root_free(allocation); return ROOT_MODULE_BAD_FORMAT; }

        const Elf32Rel* rel = (const Elf32Rel*)(file + rels->sh_offset);
        usize rel_count = rels->sh_size / sizeof(Elf32Rel);
        for (usize r = 0u; r < rel_count; r++)
        {
            u32 sym_index = ELF32_R_SYM(rel[r].r_info);
            u8 type = ELF32_R_TYPE(rel[r].r_info);
            if (type == R_386_NONE) continue;
            if (sym_index >= symbol_count || rel[r].r_offset + sizeof(u32) > sections[rels->sh_info].sh_size)
            { root_free(allocation); return ROOT_MODULE_RELOCATION_ERROR; }
            u32 S = 0u;
            if (!symbol_address(&symbols[sym_index], sections, header->e_shnum, runtime, &S))
            { root_free(allocation); return ROOT_MODULE_RELOCATION_ERROR; }
            u32* place = (u32*)(runtime[rels->sh_info] + rel[r].r_offset);
            u32 A = *place;
            u32 P = (u32)(usize)place;
            if (type == R_386_32) *place = S + A;
            else if (type == R_386_PC32) *place = S + A - P;
            else { root_free(allocation); return ROOT_MODULE_RELOCATION_ERROR; }
        }
    }

    void* entry = NULL;
    for (usize i = 0u; i < symbol_count; i++)
    {
        const char* name = table_string(strings, strings_size, symbols[i].st_name);
        if (name == NULL || !root_streq(name, entry_symbol)) continue;
        u32 address = 0u;
        if (!symbol_address(&symbols[i], sections, header->e_shnum, runtime, &address))
        { root_free(allocation); return ROOT_MODULE_NO_ENTRY; }
        entry = (void*)(usize)address;
        break;
    }
    if (entry == NULL) { root_free(allocation); return ROOT_MODULE_NO_ENTRY; }

    output->image_allocation = allocation;
    output->image_base = image;
    output->image_size = image_size;
    output->entry = entry;
    return ROOT_MODULE_OK;
}

const char* elf_module_result_string(RootModuleResult result)
{
    switch (result)
    {
        case ROOT_MODULE_OK: return "ok";
        case ROOT_MODULE_BAD_FORMAT: return "bad ELF";
        case ROOT_MODULE_UNSUPPORTED: return "unsupported ELF";
        case ROOT_MODULE_NO_MEMORY: return "out of memory";
        case ROOT_MODULE_NO_ENTRY: return "entry symbol missing";
        case ROOT_MODULE_RELOCATION_ERROR: return "relocation failed";
        default: return "module error";
    }
}
