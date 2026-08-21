#ifndef ROOTOS_ROOTDRIVER_H
#define ROOTOS_ROOTDRIVER_H

/* RootOS Driver ABI v1. Drivers are freestanding ELF32 ET_REL modules. */
typedef unsigned char      RootDrvU8;
typedef unsigned short     RootDrvU16;
typedef unsigned int       RootDrvU32;
typedef unsigned long long RootDrvU64;
typedef unsigned int       RootDrvSize;
typedef int                RootDrvBool;

#define ROOT_DRIVER_ABI_VERSION 1u
#define ROOT_DRIVER_OK 0
#define ROOT_DRIVER_ERROR (-1)

#define ROOT_DRIVER_BUS_PCI 1u
#define ROOT_DRIVER_BAR_NONE 0u
#define ROOT_DRIVER_BAR_IO 1u
#define ROOT_DRIVER_BAR_MEMORY32 2u
#define ROOT_DRIVER_BAR_MEMORY64 3u
#define ROOT_DRIVER_MAX_BARS 6u

#define ROOT_NET_FRAME_MAX 2048u

typedef struct {
    RootDrvU8 present;
    RootDrvU8 type;
    RootDrvU8 prefetchable;
    RootDrvU8 reserved;
    RootDrvU64 base;
} RootDriverBar;

typedef struct {
    RootDrvU32 root_device_id;
    RootDrvU8 bus_type;
    RootDrvU8 pci_bus;
    RootDrvU8 pci_device;
    RootDrvU8 pci_function;
    RootDrvU16 vendor_id;
    RootDrvU16 device_id;
    RootDrvU8 class_code;
    RootDrvU8 subclass;
    RootDrvU8 prog_if;
    RootDrvU8 revision_id;
    RootDriverBar bars[ROOT_DRIVER_MAX_BARS];
} RootDriverDeviceInfo;

typedef struct {
    RootDrvBool (*ready)(void* context);
    RootDrvBool (*link_up)(void* context);
    RootDrvBool (*send_frame)(void* context, const void* data, RootDrvSize size);
    RootDrvBool (*receive_frame)(void* context, void* output, RootDrvSize capacity, RootDrvSize* result_size);
} RootNetDriverOps;

typedef struct RootDriverApi {
    RootDrvU32 abi_version;

    void (*log)(const char* text);
    RootDrvU64 (*time_ms)(void);
    void (*cpu_pause)(void);
    void (*memory_barrier)(void);

    RootDrvBool (*pci_enable_memory)(RootDrvU8 bus, RootDrvU8 device, RootDrvU8 function);
    RootDrvBool (*pci_enable_bus_master)(RootDrvU8 bus, RootDrvU8 device, RootDrvU8 function);

    RootDrvU8  (*mmio_read8)(RootDrvU64 base, RootDrvU32 offset);
    RootDrvU16 (*mmio_read16)(RootDrvU64 base, RootDrvU32 offset);
    RootDrvU32 (*mmio_read32)(RootDrvU64 base, RootDrvU32 offset);
    void (*mmio_write8)(RootDrvU64 base, RootDrvU32 offset, RootDrvU8 value);
    void (*mmio_write16)(RootDrvU64 base, RootDrvU32 offset, RootDrvU16 value);
    void (*mmio_write32)(RootDrvU64 base, RootDrvU32 offset, RootDrvU32 value);

    RootDrvBool (*net_register)(
        const char* driver_name,
        void* context,
        const RootNetDriverOps* ops,
        const RootDrvU8 mac[6]
    );
} RootDriverApi;

/* Every RootOS driver module exports exactly this symbol. */
int root_driver_entry(const RootDriverApi* api, const RootDriverDeviceInfo* device);

#endif
