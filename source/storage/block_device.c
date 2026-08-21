#include "block_device.h"

#include "memory.h"
#include "string.h"

static RootBlockDevice devices[ROOT_BLOCK_MAX_DEVICES];
static u32 next_id = 0u;

void block_device_init(void)
{
    root_memzero(devices, sizeof(devices));
    next_id = 0u;
}

bool block_device_register(
    const RootBlockDevice* device,
    u32* assigned_id
)
{
    if (
        device == NULL
        || device->block_size == 0u
        || device->block_count == 0u
        || device->read == NULL
    )
    {
        return false;
    }

    for (usize i = 0u; i < ROOT_BLOCK_MAX_DEVICES; i++)
    {
        if (devices[i].used)
        {
            continue;
        }

        devices[i] = *device;
        devices[i].used = true;
        devices[i].id = next_id++;

        if (assigned_id != NULL)
        {
            *assigned_id = devices[i].id;
        }

        return true;
    }

    return false;
}

void block_device_remove_bus(RootBlockBus bus)
{
    for (usize i = 0u; i < ROOT_BLOCK_MAX_DEVICES; i++)
    {
        if (
            devices[i].used
            && devices[i].bus == bus
        )
        {
            root_memzero(&devices[i], sizeof(devices[i]));
        }
    }
}

usize block_device_count(void)
{
    usize count = 0u;

    for (usize i = 0u; i < ROOT_BLOCK_MAX_DEVICES; i++)
    {
        if (devices[i].used)
        {
            count++;
        }
    }

    return count;
}

bool block_device_get(usize index, RootBlockDevice* output)
{
    if (output == NULL)
    {
        return false;
    }

    usize current = 0u;

    for (usize i = 0u; i < ROOT_BLOCK_MAX_DEVICES; i++)
    {
        if (!devices[i].used)
        {
            continue;
        }

        if (current == index)
        {
            *output = devices[i];
            return true;
        }

        current++;
    }

    return false;
}

bool block_device_get_by_id(u32 id, RootBlockDevice* output)
{
    if (output == NULL)
    {
        return false;
    }

    for (usize i = 0u; i < ROOT_BLOCK_MAX_DEVICES; i++)
    {
        if (
            devices[i].used
            && devices[i].id == id
        )
        {
            *output = devices[i];
            return true;
        }
    }

    return false;
}

static RootBlockDevice* block_device_find_mutable(u32 id)
{
    for (usize i = 0u; i < ROOT_BLOCK_MAX_DEVICES; i++)
    {
        if (
            devices[i].used
            && devices[i].id == id
        )
        {
            return &devices[i];
        }
    }

    return NULL;
}

bool block_device_read(
    u32 id,
    u64 lba,
    u32 count,
    void* buffer
)
{
    RootBlockDevice* device = block_device_find_mutable(id);

    if (
        device == NULL
        || count == 0u
        || buffer == NULL
        || device->read == NULL
        || lba >= device->block_count
        || (u64)count > device->block_count - lba
    )
    {
        return false;
    }

    return device->read(device, lba, count, buffer);
}

bool block_device_write(
    u32 id,
    u64 lba,
    u32 count,
    const void* buffer
)
{
    RootBlockDevice* device = block_device_find_mutable(id);

    if (
        device == NULL
        || device->read_only
        || count == 0u
        || buffer == NULL
        || device->write == NULL
        || lba >= device->block_count
        || (u64)count > device->block_count - lba
    )
    {
        return false;
    }

    return device->write(device, lba, count, buffer);
}

const char* block_device_bus_name(RootBlockBus bus)
{
    switch (bus)
    {
        case ROOT_BLOCK_BUS_ATA:
            return "ATA";

        case ROOT_BLOCK_BUS_USB:
            return "USB";

        default:
            return "Unknown";
    }
}
