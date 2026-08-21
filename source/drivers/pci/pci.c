#include "pci.h"
#include "memory.h"

#define PCI_CONFIG_ADDRESS_PORT 0x0CF8u
#define PCI_CONFIG_DATA_PORT    0x0CFCu

#define PCI_VENDOR_NONE 0xFFFFu

#define PCI_HEADER_TYPE_DEVICE 0x00u
#define PCI_HEADER_TYPE_BRIDGE 0x01u
#define PCI_HEADER_TYPE_MASK   0x7Fu
#define PCI_HEADER_MULTIFUNCTION 0x80u

#define PCI_COMMAND_IO_SPACE     0x0001u
#define PCI_COMMAND_MEMORY_SPACE 0x0002u
#define PCI_COMMAND_BUS_MASTER   0x0004u

static PciDevice devices[PCI_MAX_DEVICES];
static usize device_count = 0u;

static inline void pci_out8(u16 port, u8 value)
{
    __asm__ volatile(
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
        : "memory"
    );
}

static inline void pci_out16(u16 port, u16 value)
{
    __asm__ volatile(
        "outw %0, %1"
        :
        : "a"(value), "Nd"(port)
        : "memory"
    );
}

static inline void pci_out32(u16 port, u32 value)
{
    __asm__ volatile(
        "outl %0, %1"
        :
        : "a"(value), "Nd"(port)
        : "memory"
    );
}

static inline u32 pci_in32(u16 port)
{
    u32 value;

    __asm__ volatile(
        "inl %1, %0"
        : "=a"(value)
        : "Nd"(port)
        : "memory"
    );

    return value;
}

static u32 pci_config_address(PciAddress address, u8 offset)
{
    return
        0x80000000u
        |
        ((u32)address.bus << 16)
        |
        ((u32)(address.device & 0x1Fu) << 11)
        |
        ((u32)(address.function & 0x07u) << 8)
        |
        ((u32)offset & 0xFCu);
}

u32 pci_config_read32(PciAddress address, u8 offset)
{
    pci_out32(
        PCI_CONFIG_ADDRESS_PORT,
        pci_config_address(address, offset)
    );

    return pci_in32(PCI_CONFIG_DATA_PORT);
}

u16 pci_config_read16(PciAddress address, u8 offset)
{
    u32 value = pci_config_read32(address, offset);
    u32 shift = ((u32)offset & 2u) * 8u;

    return (u16)((value >> shift) & 0xFFFFu);
}

u8 pci_config_read8(PciAddress address, u8 offset)
{
    u32 value = pci_config_read32(address, offset);
    u32 shift = ((u32)offset & 3u) * 8u;

    return (u8)((value >> shift) & 0xFFu);
}

void pci_config_write32(PciAddress address, u8 offset, u32 value)
{
    pci_out32(
        PCI_CONFIG_ADDRESS_PORT,
        pci_config_address(address, offset)
    );

    pci_out32(PCI_CONFIG_DATA_PORT, value);
}

void pci_config_write16(PciAddress address, u8 offset, u16 value)
{
    pci_out32(
        PCI_CONFIG_ADDRESS_PORT,
        pci_config_address(address, offset)
    );

    pci_out16(
        (u16)(PCI_CONFIG_DATA_PORT + (offset & 2u)),
        value
    );
}

void pci_config_write8(PciAddress address, u8 offset, u8 value)
{
    pci_out32(
        PCI_CONFIG_ADDRESS_PORT,
        pci_config_address(address, offset)
    );

    pci_out8(
        (u16)(PCI_CONFIG_DATA_PORT + (offset & 3u)),
        value
    );
}

static void pci_read_bars(PciDevice* device)
{
    if (device == NULL)
    {
        return;
    }

    u8 header =
        (u8)(device->header_type & PCI_HEADER_TYPE_MASK);

    u8 maximum_bars = 0u;

    if (header == PCI_HEADER_TYPE_DEVICE)
    {
        maximum_bars = 6u;
    }
    else if (header == PCI_HEADER_TYPE_BRIDGE)
    {
        maximum_bars = 2u;
    }

    device->bar_count = maximum_bars;

    for (u8 i = 0u; i < PCI_MAX_BARS; i++)
    {
        device->bars[i].present = false;
        device->bars[i].type = PCI_BAR_NONE;
        device->bars[i].base = 0u;
        device->bars[i].prefetchable = false;
        device->bars[i].raw_low = 0u;
        device->bars[i].raw_high = 0u;
    }

    for (u8 i = 0u; i < maximum_bars; i++)
    {
        u8 offset = (u8)(0x10u + (i * 4u));
        u32 raw =
            pci_config_read32(
                device->address,
                offset
            );

        if (raw == 0u || raw == 0xFFFFFFFFu)
        {
            continue;
        }

        PciBar* bar = &device->bars[i];
        bar->present = true;
        bar->raw_low = raw;

        if ((raw & 0x01u) != 0u)
        {
            bar->type = PCI_BAR_IO;
            bar->base = (u64)(raw & 0xFFFFFFFCu);
            continue;
        }

        bar->prefetchable = (raw & 0x08u) != 0u;

        u8 memory_type =
            (u8)((raw >> 1) & 0x03u);

        if (memory_type == 0x02u && (u8)(i + 1u) < maximum_bars)
        {
            u32 high =
                pci_config_read32(
                    device->address,
                    (u8)(offset + 4u)
                );

            bar->type = PCI_BAR_MEMORY64;
            bar->raw_high = high;
            bar->base =
                ((u64)high << 32)
                |
                (u64)(raw & 0xFFFFFFF0u);

            /* The following register is the high half, not another BAR. */
            i++;
            continue;
        }

        bar->type = PCI_BAR_MEMORY32;
        bar->base = (u64)(raw & 0xFFFFFFF0u);
    }
}

static void pci_store_device(PciAddress address)
{
    if (device_count >= PCI_MAX_DEVICES)
    {
        return;
    }

    u16 vendor = pci_config_read16(address, 0x00u);

    if (vendor == PCI_VENDOR_NONE)
    {
        return;
    }

    PciDevice* device = &devices[device_count];
    root_memzero(device, sizeof(*device));

    device->address = address;
    device->vendor_id = vendor;
    device->device_id = pci_config_read16(address, 0x02u);
    device->command = pci_config_read16(address, 0x04u);
    device->status = pci_config_read16(address, 0x06u);

    device->revision_id = pci_config_read8(address, 0x08u);
    device->prog_if = pci_config_read8(address, 0x09u);
    device->subclass = pci_config_read8(address, 0x0Au);
    device->class_code = pci_config_read8(address, 0x0Bu);

    device->cache_line_size = pci_config_read8(address, 0x0Cu);
    device->latency_timer = pci_config_read8(address, 0x0Du);
    device->header_type = pci_config_read8(address, 0x0Eu);
    device->bist = pci_config_read8(address, 0x0Fu);

    device->multifunction =
        (device->header_type & PCI_HEADER_MULTIFUNCTION) != 0u;

    u8 header =
        (u8)(device->header_type & PCI_HEADER_TYPE_MASK);

    if (header == PCI_HEADER_TYPE_DEVICE)
    {
        device->interrupt_line = pci_config_read8(address, 0x3Cu);
        device->interrupt_pin = pci_config_read8(address, 0x3Du);
    }
    else if (header == PCI_HEADER_TYPE_BRIDGE)
    {
        device->interrupt_line = pci_config_read8(address, 0x3Cu);
        device->interrupt_pin = pci_config_read8(address, 0x3Du);
    }

    pci_read_bars(device);
    device_count++;
}

static void pci_queue_bus(
    u8 bus,
    bool seen[256],
    u8 queue[256],
    u16* tail
)
{
    if (seen[bus])
    {
        return;
    }

    if (*tail >= 256u)
    {
        return;
    }

    seen[bus] = true;
    queue[*tail] = bus;
    (*tail)++;
}

void pci_rescan(void)
{
    root_memzero(devices, sizeof(devices));
    device_count = 0u;

    bool seen[256];
    u8 queue[256];
    root_memzero(seen, sizeof(seen));
    root_memzero(queue, sizeof(queue));

    u16 head = 0u;
    u16 tail = 0u;

    pci_queue_bus(0u, seen, queue, &tail);

    while (head < tail)
    {
        u8 bus = queue[head++];

        for (u8 slot = 0u; slot < 32u; slot++)
        {
            PciAddress function0;
            function0.bus = bus;
            function0.device = slot;
            function0.function = 0u;

            if (pci_config_read16(function0, 0x00u) == PCI_VENDOR_NONE)
            {
                continue;
            }

            u8 header_type = pci_config_read8(function0, 0x0Eu);
            u8 function_count =
                (header_type & PCI_HEADER_MULTIFUNCTION)
                ?
                8u
                :
                1u;

            for (u8 function = 0u; function < function_count; function++)
            {
                PciAddress address;
                address.bus = bus;
                address.device = slot;
                address.function = function;

                if (pci_config_read16(address, 0x00u) == PCI_VENDOR_NONE)
                {
                    continue;
                }

                u8 class_code = pci_config_read8(address, 0x0Bu);
                u8 subclass = pci_config_read8(address, 0x0Au);

                pci_store_device(address);

                if (
                    class_code == PCI_CLASS_BRIDGE
                    &&
                    subclass == PCI_SUBCLASS_PCI_BRIDGE
                )
                {
                    u8 secondary_bus =
                        pci_config_read8(address, 0x19u);

                    if (secondary_bus != 0u)
                    {
                        pci_queue_bus(
                            secondary_bus,
                            seen,
                            queue,
                            &tail
                        );
                    }
                }
            }
        }
    }
}

void pci_init(void)
{
    pci_rescan();
}

usize pci_device_count(void)
{
    return device_count;
}

bool pci_get_device(usize index, PciDevice* output)
{
    if (output == NULL || index >= device_count)
    {
        return false;
    }

    root_memcpy(
        output,
        &devices[index],
        sizeof(*output)
    );

    return true;
}

bool pci_find_class(
    u8 class_code,
    u8 subclass,
    u8 prog_if,
    usize start_index,
    usize* found_index,
    PciDevice* output
)
{
    for (usize i = start_index; i < device_count; i++)
    {
        const PciDevice* device = &devices[i];

        if (
            device->class_code != class_code
            ||
            device->subclass != subclass
            ||
            device->prog_if != prog_if
        )
        {
            continue;
        }

        if (found_index != NULL)
        {
            *found_index = i;
        }

        if (output != NULL)
        {
            root_memcpy(
                output,
                device,
                sizeof(*output)
            );
        }

        return true;
    }

    return false;
}

static bool pci_enable_command_bit(PciAddress address, u16 bit)
{
    if (pci_config_read16(address, 0x00u) == PCI_VENDOR_NONE)
    {
        return false;
    }

    u16 command = pci_config_read16(address, 0x04u);

    if ((command & bit) == 0u)
    {
        command = (u16)(command | bit);
        pci_config_write16(address, 0x04u, command);
    }

    return
        (pci_config_read16(address, 0x04u) & bit)
        !=
        0u;
}

bool pci_enable_io(PciAddress address)
{
    return pci_enable_command_bit(address, PCI_COMMAND_IO_SPACE);
}

bool pci_enable_memory(PciAddress address)
{
    return pci_enable_command_bit(address, PCI_COMMAND_MEMORY_SPACE);
}

bool pci_enable_bus_mastering(PciAddress address)
{
    return pci_enable_command_bit(address, PCI_COMMAND_BUS_MASTER);
}

const char* pci_class_name(u8 class_code)
{
    switch (class_code)
    {
        case 0x00u: return "Unclassified";
        case 0x01u: return "Mass storage";
        case 0x02u: return "Network";
        case 0x03u: return "Display";
        case 0x04u: return "Multimedia";
        case 0x05u: return "Memory";
        case 0x06u: return "Bridge";
        case 0x07u: return "Communication";
        case 0x08u: return "System peripheral";
        case 0x09u: return "Input";
        case 0x0Au: return "Docking";
        case 0x0Bu: return "Processor";
        case 0x0Cu: return "Serial bus";
        case 0x0Du: return "Wireless";
        case 0x0Eu: return "Intelligent I/O";
        case 0x0Fu: return "Satellite";
        case 0x10u: return "Encryption";
        case 0x11u: return "Signal processing";
        case 0x12u: return "Processing accelerator";
        case 0x13u: return "Instrumentation";
        default: return "Unknown";
    }
}

const char* pci_subclass_name(u8 class_code, u8 subclass, u8 prog_if)
{
    if (class_code == 0x01u)
    {
        switch (subclass)
        {
            case 0x00u: return "SCSI storage";
            case 0x01u: return "IDE controller";
            case 0x02u: return "Floppy controller";
            case 0x05u: return "ATA controller";
            case 0x06u: return "SATA controller";
            case 0x07u: return "SAS controller";
            case 0x08u: return "NVM controller";
            default: return "Mass storage controller";
        }
    }

    if (class_code == 0x02u)
    {
        switch (subclass)
        {
            case 0x00u: return "Ethernet controller";
            case 0x80u: return "Network controller";
            default: return "Network controller";
        }
    }

    if (class_code == 0x03u)
    {
        switch (subclass)
        {
            case 0x00u: return "VGA controller";
            case 0x02u: return "3D controller";
            default: return "Display controller";
        }
    }

    if (class_code == 0x06u)
    {
        switch (subclass)
        {
            case 0x00u: return "Host bridge";
            case 0x01u: return "ISA bridge";
            case 0x04u: return "PCI bridge";
            default: return "Bridge device";
        }
    }

    if (class_code == PCI_CLASS_SERIAL_BUS && subclass == PCI_SUBCLASS_USB)
    {
        switch (prog_if)
        {
            case 0x00u: return "USB UHCI controller";
            case 0x10u: return "USB OHCI controller";
            case 0x20u: return "USB EHCI controller";
            case 0x30u: return "USB xHCI controller";
            case 0x80u: return "USB controller";
            case 0xFEu: return "USB device";
            default: return "USB controller";
        }
    }

    return pci_class_name(class_code);
}

const char* pci_bar_type_name(PciBarType type)
{
    switch (type)
    {
        case PCI_BAR_IO: return "I/O";
        case PCI_BAR_MEMORY32: return "MMIO32";
        case PCI_BAR_MEMORY64: return "MMIO64";
        default: return "none";
    }
}
