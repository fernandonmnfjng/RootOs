#ifndef ROOTOS_PCI_H
#define ROOTOS_PCI_H

#include "types.h"

#define PCI_MAX_DEVICES 128u
#define PCI_MAX_BARS 6u

#define PCI_CLASS_BRIDGE        0x06u
#define PCI_SUBCLASS_PCI_BRIDGE 0x04u
#define PCI_CLASS_SERIAL_BUS    0x0Cu
#define PCI_SUBCLASS_USB        0x03u
#define PCI_PROGIF_XHCI         0x30u

typedef struct
{
    u8 bus;
    u8 device;
    u8 function;
} PciAddress;

typedef enum
{
    PCI_BAR_NONE = 0,
    PCI_BAR_IO,
    PCI_BAR_MEMORY32,
    PCI_BAR_MEMORY64
} PciBarType;

typedef struct
{
    bool present;
    PciBarType type;
    u64 base;
    bool prefetchable;
    u32 raw_low;
    u32 raw_high;
} PciBar;

typedef struct
{
    PciAddress address;

    u16 vendor_id;
    u16 device_id;

    u16 command;
    u16 status;

    u8 revision_id;
    u8 prog_if;
    u8 subclass;
    u8 class_code;

    u8 cache_line_size;
    u8 latency_timer;
    u8 header_type;
    u8 bist;

    u8 interrupt_line;
    u8 interrupt_pin;

    PciBar bars[PCI_MAX_BARS];
    u8 bar_count;

    bool multifunction;
} PciDevice;

void pci_init(void);
void pci_rescan(void);

usize pci_device_count(void);
bool pci_get_device(usize index, PciDevice* output);

bool pci_find_class(
    u8 class_code,
    u8 subclass,
    u8 prog_if,
    usize start_index,
    usize* found_index,
    PciDevice* output
);

u8 pci_config_read8(PciAddress address, u8 offset);
u16 pci_config_read16(PciAddress address, u8 offset);
u32 pci_config_read32(PciAddress address, u8 offset);

void pci_config_write8(PciAddress address, u8 offset, u8 value);
void pci_config_write16(PciAddress address, u8 offset, u16 value);
void pci_config_write32(PciAddress address, u8 offset, u32 value);

bool pci_enable_io(PciAddress address);
bool pci_enable_memory(PciAddress address);
bool pci_enable_bus_mastering(PciAddress address);

const char* pci_class_name(u8 class_code);
const char* pci_subclass_name(u8 class_code, u8 subclass, u8 prog_if);
const char* pci_bar_type_name(PciBarType type);

#endif
