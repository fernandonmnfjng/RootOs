#include "driver_api.h"
#include "pci.h"
#include "net_device.h"
#include "terminal.h"
#include "time.h"
#include "memory.h"

static void api_log(const char* text)
{
    if (text != NULL) terminal_print(text);
}

static u64 api_time_ms(void) { return root_time_millis(); }
static void api_cpu_pause(void) { __asm__ volatile ("pause"); }
static void api_memory_barrier(void) { __asm__ volatile ("" ::: "memory"); }

static PciAddress api_address(u8 bus, u8 device, u8 function)
{
    PciAddress a; a.bus = bus; a.device = device; a.function = function; return a;
}

static RootDrvBool api_pci_enable_memory(u8 bus, u8 device, u8 function)
{ return pci_enable_memory(api_address(bus, device, function)) ? 1 : 0; }
static RootDrvBool api_pci_enable_bus_master(u8 bus, u8 device, u8 function)
{ return pci_enable_bus_mastering(api_address(bus, device, function)) ? 1 : 0; }

static u8 api_mmio_read8(u64 base, u32 offset)
{ return *(volatile u8*)(usize)((u32)base + offset); }
static u16 api_mmio_read16(u64 base, u32 offset)
{ return *(volatile u16*)(usize)((u32)base + offset); }
static u32 api_mmio_read32(u64 base, u32 offset)
{ return *(volatile u32*)(usize)((u32)base + offset); }
static void api_mmio_write8(u64 base, u32 offset, u8 value)
{ *(volatile u8*)(usize)((u32)base + offset) = value; __asm__ volatile ("" ::: "memory"); }
static void api_mmio_write16(u64 base, u32 offset, u16 value)
{ *(volatile u16*)(usize)((u32)base + offset) = value; __asm__ volatile ("" ::: "memory"); }
static void api_mmio_write32(u64 base, u32 offset, u32 value)
{ *(volatile u32*)(usize)((u32)base + offset) = value; __asm__ volatile ("" ::: "memory"); }

static RootDrvBool api_net_register(
    const char* driver_name,
    void* context,
    const RootNetDriverOps* ops,
    const u8 mac[6]
)
{
    if (ops == NULL) return false;
    RootNetDeviceOps native;
    native.ready = (bool (*)(void*))ops->ready;
    native.link_up = (bool (*)(void*))ops->link_up;
    native.send_frame = (bool (*)(void*, const void*, usize))ops->send_frame;
    native.receive_frame = (bool (*)(void*, void*, usize, usize*))ops->receive_frame;
    return net_device_register(driver_name, context, &native, mac) ? 1 : 0;
}

static RootDriverApi api;

void driver_api_init(void)
{
    root_memzero(&api, sizeof(api));
    api.abi_version = ROOT_DRIVER_ABI_VERSION;
    api.log = api_log;
    api.time_ms = api_time_ms;
    api.cpu_pause = api_cpu_pause;
    api.memory_barrier = api_memory_barrier;
    api.pci_enable_memory = api_pci_enable_memory;
    api.pci_enable_bus_master = api_pci_enable_bus_master;
    api.mmio_read8 = api_mmio_read8;
    api.mmio_read16 = api_mmio_read16;
    api.mmio_read32 = api_mmio_read32;
    api.mmio_write8 = api_mmio_write8;
    api.mmio_write16 = api_mmio_write16;
    api.mmio_write32 = api_mmio_write32;
    api.net_register = api_net_register;
}

const RootDriverApi* driver_api(void) { return &api; }

void driver_api_make_device(const RootDevice* source, RootDriverDeviceInfo* output)
{
    if (output == NULL) return;
    root_memzero(output, sizeof(*output));
    if (source == NULL || source->bus != ROOT_DEVICE_BUS_PCI) return;
    output->root_device_id = source->id;
    output->bus_type = ROOT_DRIVER_BUS_PCI;
    output->pci_bus = source->pci.address.bus;
    output->pci_device = source->pci.address.device;
    output->pci_function = source->pci.address.function;
    output->vendor_id = source->pci.vendor_id;
    output->device_id = source->pci.device_id;
    output->class_code = source->pci.class_code;
    output->subclass = source->pci.subclass;
    output->prog_if = source->pci.prog_if;
    output->revision_id = source->pci.revision_id;
    for (u32 i = 0u; i < ROOT_DRIVER_MAX_BARS; i++)
    {
        output->bars[i].present = source->pci.bars[i].present ? 1u : 0u;
        output->bars[i].type = (u8)source->pci.bars[i].type;
        output->bars[i].prefetchable = source->pci.bars[i].prefetchable ? 1u : 0u;
        output->bars[i].base = source->pci.bars[i].base;
    }
}
