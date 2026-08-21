#ifndef ROOTOS_ROOTSTORAGE_H
#define ROOTOS_ROOTSTORAGE_H

#include "types.h"

#define ROOT_STORAGE_SECTOR_SIZE 512u
#define ROOT_STORAGE_HEADER_LBA  2048u

/*
 * ROOTFS42 storage geometry.
 *
 * 2 sectors metadata + 128 sectors payload = 64 KiB/file.
 * 256 slots consume ~16.25 MiB, comfortably inside the default 64 MiB disk.
 */
#define ROOT_STORAGE_FORMAT_VERSION 2u
#define ROOT_STORAGE_NODE_COUNT 256u
#define ROOT_STORAGE_NODE_SLOT_SECTORS 130u

bool rootstorage_init(void);

bool rootstorage_device_available(void);
bool rootstorage_volume_available(void);

u32 rootstorage_device_sector_count(void);
u32 rootstorage_volume_sector_count(void);
u32 rootstorage_node_count(void);
u32 rootstorage_node_slot_sectors(void);
u32 rootstorage_node_area_lba(void);

bool rootstorage_read_sector(
    u32 lba,
    void* buffer
);

/*
 * Safety rule:
 * writes are rejected unless a valid RootOS volume signature was detected.
 */
bool rootstorage_write_sector(
    u32 lba,
    const void* buffer
);

#endif
