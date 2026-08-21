#ifndef ROOTOS_ROOTFS_DISK_H
#define ROOTOS_ROOTFS_DISK_H

#include "types.h"

#define ROOTFS_DISK_MAX_NODES 256u
#define ROOTFS_DISK_PATH_MAX 512u
#define ROOTFS_DISK_FILE_MAX (64u * 1024u)

typedef enum
{
    ROOTFS_DISK_DIRECTORY = 0,
    ROOTFS_DISK_FILE = 1
} RootFsDiskType;

typedef struct
{
    bool used;
    RootFsDiskType type;
    usize size;
    char path[ROOTFS_DISK_PATH_MAX];
} RootFsDiskNode;

bool rootfs_disk_ready(void);

bool rootfs_disk_read_node(
    u32 index,
    RootFsDiskNode* node
);

bool rootfs_disk_write_node(
    u32 index,
    const RootFsDiskNode* node
);

bool rootfs_disk_read_file(
    u32 index,
    void* output,
    usize capacity,
    usize* result_size
);

bool rootfs_disk_write_file(
    u32 index,
    const void* data,
    usize size
);

bool rootfs_disk_clear_node(u32 index);

#endif
