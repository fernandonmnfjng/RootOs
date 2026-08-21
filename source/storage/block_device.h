#ifndef ROOTOS_BLOCK_DEVICE_H
#define ROOTOS_BLOCK_DEVICE_H

#include "types.h"

#define ROOT_BLOCK_MAX_DEVICES 16u
#define ROOT_BLOCK_NAME_MAX 48u
#define ROOT_BLOCK_MODEL_MAX 64u

typedef enum
{
    ROOT_BLOCK_BUS_UNKNOWN = 0,
    ROOT_BLOCK_BUS_ATA,
    ROOT_BLOCK_BUS_USB
} RootBlockBus;

typedef struct RootBlockDevice RootBlockDevice;

typedef bool (*RootBlockReadFn)(
    RootBlockDevice* device,
    u64 lba,
    u32 count,
    void* buffer
);

typedef bool (*RootBlockWriteFn)(
    RootBlockDevice* device,
    u64 lba,
    u32 count,
    const void* buffer
);

struct RootBlockDevice
{
    u32 id;
    bool used;
    bool read_only;
    RootBlockBus bus;

    char name[ROOT_BLOCK_NAME_MAX];
    char model[ROOT_BLOCK_MODEL_MAX];

    u32 block_size;
    u64 block_count;

    void* driver_data;
    RootBlockReadFn read;
    RootBlockWriteFn write;
};

void block_device_init(void);

bool block_device_register(
    const RootBlockDevice* device,
    u32* assigned_id
);

void block_device_remove_bus(RootBlockBus bus);

usize block_device_count(void);
bool block_device_get(usize index, RootBlockDevice* output);
bool block_device_get_by_id(u32 id, RootBlockDevice* output);

bool block_device_read(
    u32 id,
    u64 lba,
    u32 count,
    void* buffer
);

bool block_device_write(
    u32 id,
    u64 lba,
    u32 count,
    const void* buffer
);

const char* block_device_bus_name(RootBlockBus bus);

#endif
