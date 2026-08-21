#ifndef ROOTOS_RTPGK_H
#define ROOTOS_RTPGK_H

#include "types.h"

#define RTPGK_MAGIC_SIZE 8u
#define RTPGK_VERSION 1u
#define RTPGK_HEADER_SIZE 256u
#define RTPGK_PATH_MAX 256u
#define RTPGK_NAME_MAX 64u
#define RTPGK_VERSION_TEXT_MAX 32u
#define RTPGK_ENTRY_NAME_MAX 96u
#define RTPGK_TYPE_APPLICATION 1u

typedef struct __attribute__((packed))
{
    u8 magic[8];
    u32 version;
    u32 header_size;
    u32 entry_count;
    u32 table_offset;
    u32 data_offset;
    u32 total_size;
    u32 package_type;
    char name[RTPGK_NAME_MAX];
    char version_text[RTPGK_VERSION_TEXT_MAX];
    char application_entry[RTPGK_ENTRY_NAME_MAX];
    u8 reserved[28];
} RtpgkHeader;

typedef struct __attribute__((packed))
{
    char path[RTPGK_PATH_MAX];
    u32 data_offset;
    u32 size;
    u32 flags;
} RtpgkEntry;

_Static_assert(
    sizeof(RtpgkHeader) == RTPGK_HEADER_SIZE,
    "RtpgkHeader must be 256 bytes"
);

_Static_assert(
    sizeof(RtpgkEntry) == 268u,
    "RtpgkEntry layout changed"
);

bool rtpgk_validate(
    const void* data,
    usize size,
    const RtpgkHeader** header
);

bool rtpgk_entry(
    const void* data,
    usize size,
    const RtpgkHeader* header,
    u32 index,
    const RtpgkEntry** entry,
    const u8** payload
);

bool rtpgk_safe_name(const char* name);
bool rtpgk_safe_relative_path(const char* path);

#endif
