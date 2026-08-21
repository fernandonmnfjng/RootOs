#include "rootfs_disk.h"

#include "rootstorage.h"
#include "memory.h"

#define ROOTFS_NODE_METADATA_SECTORS 2u
#define ROOTFS_NODE_DATA_SECTORS 128u
#define ROOTFS_NODE_RECORD_BYTES \
    (ROOTFS_NODE_METADATA_SECTORS * ROOT_STORAGE_SECTOR_SIZE)

#define ROOTFS_NODE_MAGIC_0 'N'
#define ROOTFS_NODE_MAGIC_1 'O'
#define ROOTFS_NODE_MAGIC_2 'D'
#define ROOTFS_NODE_MAGIC_3 'E'
#define ROOTFS_NODE_VERSION 2u

static u8 metadata_buffer[ROOTFS_NODE_RECORD_BYTES];
static u8 sector_buffer[ROOT_STORAGE_SECTOR_SIZE];

static void write_u16(u8* destination, u16 value)
{
    destination[0] = (u8)(value & 0xFFu);
    destination[1] = (u8)(value >> 8);
}

static u16 read_u16(const u8* source)
{
    return
        (u16)source[0]
        |
        ((u16)source[1] << 8);
}

static void write_u32(u8* destination, u32 value)
{
    destination[0] = (u8)(value & 0xFFu);
    destination[1] = (u8)((value >> 8) & 0xFFu);
    destination[2] = (u8)((value >> 16) & 0xFFu);
    destination[3] = (u8)((value >> 24) & 0xFFu);
}

static u32 read_u32(const u8* source)
{
    return
        (u32)source[0]
        |
        ((u32)source[1] << 8)
        |
        ((u32)source[2] << 16)
        |
        ((u32)source[3] << 24);
}

static usize bounded_path_length(const char* path)
{
    usize length = 0;

    if (path == NULL)
    {
        return 0;
    }

    while (
        length < ROOTFS_DISK_PATH_MAX - 1
        &&
        path[length] != '\0'
    )
    {
        length++;
    }

    return length;
}

static bool rootfs_node_lba(
    u32 index,
    u32* metadata_lba,
    u32* data_lba
)
{
    if (
        !rootstorage_volume_available()
        ||
        index >= ROOTFS_DISK_MAX_NODES
        ||
        metadata_lba == NULL
        ||
        data_lba == NULL
    )
    {
        return false;
    }

    u32 slot_sectors =
        rootstorage_node_slot_sectors();

    if (
        slot_sectors
        <
        ROOTFS_NODE_METADATA_SECTORS
        +
        ROOTFS_NODE_DATA_SECTORS
    )
    {
        return false;
    }

    u32 base =
        rootstorage_node_area_lba()
        +
        index * slot_sectors;

    *metadata_lba = base;
    *data_lba = base + ROOTFS_NODE_METADATA_SECTORS;

    return true;
}

static bool read_metadata(
    u32 lba,
    u8* output
)
{
    for (u32 i = 0; i < ROOTFS_NODE_METADATA_SECTORS; i++)
    {
        if (
            !rootstorage_read_sector(
                lba + i,
                output + i * ROOT_STORAGE_SECTOR_SIZE
            )
        )
        {
            return false;
        }
    }

    return true;
}

static bool write_metadata(
    u32 lba,
    const u8* input
)
{
    for (u32 i = 0; i < ROOTFS_NODE_METADATA_SECTORS; i++)
    {
        if (
            !rootstorage_write_sector(
                lba + i,
                input + i * ROOT_STORAGE_SECTOR_SIZE
            )
        )
        {
            return false;
        }
    }

    return true;
}

bool rootfs_disk_ready(void)
{
    return
        rootstorage_volume_available()
        &&
        rootstorage_node_count() == ROOTFS_DISK_MAX_NODES
        &&
        rootstorage_node_slot_sectors() >= 130u;
}

bool rootfs_disk_read_node(
    u32 index,
    RootFsDiskNode* node
)
{
    if (node == NULL)
    {
        return false;
    }

    root_memzero(node, sizeof(*node));

    u32 metadata_lba;
    u32 data_lba;

    if (!rootfs_node_lba(index, &metadata_lba, &data_lba))
    {
        return false;
    }

    (void)data_lba;

    if (!read_metadata(metadata_lba, metadata_buffer))
    {
        return false;
    }

    /* Empty slot: two zero metadata sectors. */
    if (
        metadata_buffer[0] == 0
        &&
        metadata_buffer[1] == 0
        &&
        metadata_buffer[2] == 0
        &&
        metadata_buffer[3] == 0
    )
    {
        return true;
    }

    if (
        metadata_buffer[0] != ROOTFS_NODE_MAGIC_0
        ||
        metadata_buffer[1] != ROOTFS_NODE_MAGIC_1
        ||
        metadata_buffer[2] != ROOTFS_NODE_MAGIC_2
        ||
        metadata_buffer[3] != ROOTFS_NODE_MAGIC_3
        ||
        metadata_buffer[4] != ROOTFS_NODE_VERSION
    )
    {
        return false;
    }

    u8 used = metadata_buffer[5];
    u8 type = metadata_buffer[6];
    u32 size = read_u32(metadata_buffer + 8);
    u16 path_length = read_u16(metadata_buffer + 12);

    if (
        used > 1
        ||
        type > ROOTFS_DISK_FILE
        ||
        size > ROOTFS_DISK_FILE_MAX
        ||
        path_length >= ROOTFS_DISK_PATH_MAX
    )
    {
        return false;
    }

    node->used = used != 0;
    node->type = (RootFsDiskType)type;
    node->size = size;

    root_memcpy(
        node->path,
        metadata_buffer + 16,
        path_length
    );

    node->path[path_length] = '\0';

    return true;
}

bool rootfs_disk_write_node(
    u32 index,
    const RootFsDiskNode* node
)
{
    if (
        node == NULL
        ||
        node->size > ROOTFS_DISK_FILE_MAX
    )
    {
        return false;
    }

    usize path_length =
        bounded_path_length(node->path);

    if (
        node->path[path_length] != '\0'
        ||
        path_length >= ROOTFS_DISK_PATH_MAX
    )
    {
        return false;
    }

    u32 metadata_lba;
    u32 data_lba;

    if (!rootfs_node_lba(index, &metadata_lba, &data_lba))
    {
        return false;
    }

    (void)data_lba;

    root_memzero(
        metadata_buffer,
        sizeof(metadata_buffer)
    );

    metadata_buffer[0] = ROOTFS_NODE_MAGIC_0;
    metadata_buffer[1] = ROOTFS_NODE_MAGIC_1;
    metadata_buffer[2] = ROOTFS_NODE_MAGIC_2;
    metadata_buffer[3] = ROOTFS_NODE_MAGIC_3;
    metadata_buffer[4] = ROOTFS_NODE_VERSION;
    metadata_buffer[5] = node->used ? 1u : 0u;
    metadata_buffer[6] = (u8)node->type;

    write_u32(
        metadata_buffer + 8,
        (u32)node->size
    );

    write_u16(
        metadata_buffer + 12,
        (u16)path_length
    );

    root_memcpy(
        metadata_buffer + 16,
        node->path,
        path_length
    );

    return
        write_metadata(
            metadata_lba,
            metadata_buffer
        );
}

bool rootfs_disk_read_file(
    u32 index,
    void* output,
    usize capacity,
    usize* result_size
)
{
    if (result_size != NULL)
    {
        *result_size = 0;
    }

    if (output == NULL)
    {
        return false;
    }

    RootFsDiskNode node;

    if (
        !rootfs_disk_read_node(index, &node)
        ||
        !node.used
        ||
        node.type != ROOTFS_DISK_FILE
        ||
        node.size > capacity
    )
    {
        return false;
    }

    u32 metadata_lba;
    u32 data_lba;

    if (!rootfs_node_lba(index, &metadata_lba, &data_lba))
    {
        return false;
    }

    (void)metadata_lba;

    u8* destination = (u8*)output;
    usize remaining = node.size;
    usize offset = 0;

    for (
        u32 sector = 0;
        sector < ROOTFS_NODE_DATA_SECTORS && remaining > 0;
        sector++
    )
    {
        if (
            !rootstorage_read_sector(
                data_lba + sector,
                sector_buffer
            )
        )
        {
            return false;
        }

        usize amount =
            remaining > ROOT_STORAGE_SECTOR_SIZE
            ?
            ROOT_STORAGE_SECTOR_SIZE
            :
            remaining;

        root_memcpy(
            destination + offset,
            sector_buffer,
            amount
        );

        offset += amount;
        remaining -= amount;
    }

    if (result_size != NULL)
    {
        *result_size = node.size;
    }

    return true;
}

bool rootfs_disk_write_file(
    u32 index,
    const void* data,
    usize size
)
{
    if (
        size > ROOTFS_DISK_FILE_MAX
        ||
        (size > 0 && data == NULL)
    )
    {
        return false;
    }

    RootFsDiskNode node;

    if (
        !rootfs_disk_read_node(index, &node)
        ||
        !node.used
        ||
        node.type != ROOTFS_DISK_FILE
    )
    {
        return false;
    }

    u32 metadata_lba;
    u32 data_lba;

    if (!rootfs_node_lba(index, &metadata_lba, &data_lba))
    {
        return false;
    }

    (void)metadata_lba;

    const u8* source = (const u8*)data;
    usize remaining = size;
    usize offset = 0;
    u32 sector = 0u;

    /*
     * Only sectors that belong to the new file are written. Data beyond the
     * metadata size is unreachable, so clearing all 64 KiB on every tiny
     * write would only make RootEdit/package installation unnecessarily slow.
     */
    while (remaining > 0u)
    {
        root_memzero(
            sector_buffer,
            sizeof(sector_buffer)
        );

        usize amount =
            remaining > ROOT_STORAGE_SECTOR_SIZE
            ?
            ROOT_STORAGE_SECTOR_SIZE
            :
            remaining;

        root_memcpy(
            sector_buffer,
            source + offset,
            amount
        );

        if (
            !rootstorage_write_sector(
                data_lba + sector,
                sector_buffer
            )
        )
        {
            return false;
        }

        offset += amount;
        remaining -= amount;
        sector++;
    }

    node.size = size;

    return
        rootfs_disk_write_node(
            index,
            &node
        );
}

bool rootfs_disk_clear_node(u32 index)
{
    u32 metadata_lba;
    u32 data_lba;

    if (!rootfs_node_lba(index, &metadata_lba, &data_lba))
    {
        return false;
    }

    root_memzero(
        metadata_buffer,
        sizeof(metadata_buffer)
    );

    if (!write_metadata(metadata_lba, metadata_buffer))
    {
        return false;
    }

    /*
     * Clearing metadata is enough: stale payload sectors are not addressable
     * without a valid NODE record and will be overwritten if the slot is reused.
     */
    (void)data_lba;
    return true;
}
