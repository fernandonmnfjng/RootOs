#ifndef ROOTOS_DEVICE_MANAGER_H
#define ROOTOS_DEVICE_MANAGER_H

#include "types.h"
#include "pci.h"

#define ROOT_DEVICE_MAX 128u
#define ROOT_DRIVER_MAX 32u

#define ROOT_DRIVER_ANY_VENDOR 0xFFFFu
#define ROOT_DRIVER_ANY_DEVICE 0xFFFFu
#define ROOT_DRIVER_ANY_CLASS  0xFFu

typedef enum
{
    ROOT_DEVICE_BUS_NONE = 0,
    ROOT_DEVICE_BUS_PCI
} RootDeviceBus;

typedef enum
{
    ROOT_DEVICE_DRIVER_UNBOUND = 0,
    ROOT_DEVICE_DRIVER_BOUND,
    ROOT_DEVICE_DRIVER_FAILED
} RootDeviceDriverState;

typedef struct
{
    u16 vendor_id;
    u16 device_id;
    u8 class_code;
    u8 subclass;
    u8 prog_if;
} RootDriverMatch;

typedef struct RootDevice
{
    u32 id;
    RootDeviceBus bus;
    RootDeviceDriverState driver_state;
    const char* driver_name;
    const char* display_name;
    PciDevice pci;
} RootDevice;

typedef bool (*RootDriverProbeFn)(const RootDevice* device);
typedef bool (*RootDriverAttachFn)(RootDevice* device);
typedef void (*RootDriverDetachFn)(RootDevice* device);

typedef struct
{
    const char* name;
    RootDriverMatch match;
    RootDriverProbeFn probe;
    RootDriverAttachFn attach;
    RootDriverDetachFn detach;
} RootDriver;

void device_manager_init(void);
void device_manager_rescan(void);

usize device_manager_count(void);
bool device_manager_get(usize index, RootDevice* output);

bool device_manager_find_pci_class(
    u8 class_code,
    u8 subclass,
    u8 prog_if,
    usize start_index,
    usize* found_index,
    RootDevice* output
);

bool device_manager_register_driver(const RootDriver* driver);
bool device_manager_mark_external_bound(u32 device_id, const char* driver_name);
bool device_manager_mark_external_failed(u32 device_id, const char* driver_name);
void device_manager_bind_drivers(void);

usize device_manager_driver_count(void);
bool device_manager_get_driver(usize index, const RootDriver** output);

const char* device_manager_bus_name(RootDeviceBus bus);
const char* device_manager_driver_state_name(RootDeviceDriverState state);

#endif
