#include "rootstorage.h"

#include "ata_pio.h"
#include "memory.h"

#define ROOT_STORAGE_MAGIC_SIZE 8u

static const u8 root_storage_magic[ROOT_STORAGE_MAGIC_SIZE] =
{
    'R', 'O', 'O', 'T', 'F', 'S', '4', '2'
};

typedef struct __attribute__((packed))
{
    u8 magic[8];
    u32 version;
    u32 sector_size;
    u32 node_count;
    u32 node_slot_sectors;
    u32 node_area_lba;
    u32 volume_sectors;
    u32 flags;
    u8 reserved[ROOT_STORAGE_SECTOR_SIZE - 36u];
} RootStorageHeader;

_Static_assert(
    sizeof(RootStorageHeader) == ROOT_STORAGE_SECTOR_SIZE,
    "RootStorageHeader must be exactly one sector"
);

static AtaPioDevice storage_device;
static bool storage_device_ready = false;
static bool storage_volume_ready = false;
static RootStorageHeader storage_header;

static bool rootstorage_magic_valid(const RootStorageHeader* header)
{
    for (u32 i = 0; i < ROOT_STORAGE_MAGIC_SIZE; i++)
    {
        if (header->magic[i] != root_storage_magic[i])
        {
            return false;
        }
    }

    return true;
}

static bool rootstorage_header_valid(const RootStorageHeader* header)
{
    if (header == NULL)
    {
        return false;
    }

    if (!rootstorage_magic_valid(header))
    {
        return false;
    }

    if (
        header->version != ROOT_STORAGE_FORMAT_VERSION
        ||
        header->sector_size != ROOT_STORAGE_SECTOR_SIZE
        ||
        header->node_count != ROOT_STORAGE_NODE_COUNT
        ||
        header->node_slot_sectors != ROOT_STORAGE_NODE_SLOT_SECTORS
    )
    {
        return false;
    }

    if (
        header->node_area_lba <= ROOT_STORAGE_HEADER_LBA
        ||
        header->node_area_lba >= storage_device.sector_count
    )
    {
        return false;
    }

    u32 required_sectors =
        ROOT_STORAGE_NODE_COUNT
        *
        ROOT_STORAGE_NODE_SLOT_SECTORS;

    if (
        required_sectors
        >
        storage_device.sector_count - header->node_area_lba
    )
    {
        return false;
    }

    if (
        header->volume_sectors == 0
        ||
        header->volume_sectors
        >
        storage_device.sector_count - ROOT_STORAGE_HEADER_LBA
    )
    {
        return false;
    }

    return true;
}

bool rootstorage_init(void)
{
    storage_device_ready = false;
    storage_volume_ready = false;

    root_memzero(
        &storage_device,
        sizeof(storage_device)
    );

    root_memzero(
        &storage_header,
        sizeof(storage_header)
    );

    if (!ata_pio_init(&storage_device))
    {
        return false;
    }

    storage_device_ready = true;

    if (
        ROOT_STORAGE_HEADER_LBA
        >=
        storage_device.sector_count
    )
    {
        return true;
    }

    if (
        !ata_pio_read_sector(
            &storage_device,
            ROOT_STORAGE_HEADER_LBA,
            &storage_header
        )
    )
    {
        return true;
    }

    storage_volume_ready =
        rootstorage_header_valid(
            &storage_header
        );

    return true;
}

bool rootstorage_device_available(void)
{
    return storage_device_ready;
}

bool rootstorage_volume_available(void)
{
    return storage_volume_ready;
}

u32 rootstorage_device_sector_count(void)
{
    return
        storage_device_ready
        ?
        storage_device.sector_count
        :
        0;
}

u32 rootstorage_volume_sector_count(void)
{
    return
        storage_volume_ready
        ?
        storage_header.volume_sectors
        :
        0;
}

u32 rootstorage_node_count(void)
{
    return
        storage_volume_ready
        ?
        storage_header.node_count
        :
        0;
}

u32 rootstorage_node_slot_sectors(void)
{
    return
        storage_volume_ready
        ?
        storage_header.node_slot_sectors
        :
        0;
}

u32 rootstorage_node_area_lba(void)
{
    return
        storage_volume_ready
        ?
        storage_header.node_area_lba
        :
        0;
}

bool rootstorage_read_sector(
    u32 lba,
    void* buffer
)
{
    if (
        !storage_device_ready
        ||
        buffer == NULL
        ||
        lba >= storage_device.sector_count
    )
    {
        return false;
    }

    return
        ata_pio_read_sector(
            &storage_device,
            lba,
            buffer
        );
}

bool rootstorage_write_sector(
    u32 lba,
    const void* buffer
)
{
    if (
        !storage_device_ready
        ||
        !storage_volume_ready
        ||
        buffer == NULL
    )
    {
        return false;
    }

    /*
     * Never permit the generic RootOS storage layer to overwrite sectors
     * before its own signed volume. This protection matters when RootOS is
     * eventually booted on real hardware beside another operating system.
     */
    if (
        lba <= ROOT_STORAGE_HEADER_LBA
        ||
        lba >= storage_device.sector_count
    )
    {
        return false;
    }

    return
        ata_pio_write_sector(
            &storage_device,
            lba,
            buffer
        );
}
