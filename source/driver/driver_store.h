#ifndef ROOTOS_DRIVER_STORE_H
#define ROOTOS_DRIVER_STORE_H

#include "types.h"

#define ROOT_DRIVERPACK_NAME_MAX 32u
#define ROOT_DRIVERPACK_DESCRIPTION_MAX 64u
#define ROOT_DRIVERPACK_MATCH_MAX 8u
#define ROOT_DRIVERPACK_LOADED_MAX 16u

typedef struct
{
    u16 vendor_id;
    u16 device_id;
    u8 class_code;
    u8 subclass;
    u8 prog_if;
} RootDriverPackMatchInfo;

typedef struct
{
    char name[ROOT_DRIVERPACK_NAME_MAX];
    char description[ROOT_DRIVERPACK_DESCRIPTION_MAX];
    u8 compression;
    u8 match_count;
    u32 compressed_size;
    u32 uncompressed_size;
    bool loaded;
    RootDriverPackMatchInfo matches[ROOT_DRIVERPACK_MATCH_MAX];
} RootDriverPackEntryInfo;

void driver_store_init(u32 multiboot_magic, u32 multiboot_info_address);
void driver_store_bind_all(void);

bool driver_store_available(void);
usize driver_store_entry_count(void);
bool driver_store_get(usize index, RootDriverPackEntryInfo* output);
usize driver_store_loaded_count(void);
const char* driver_store_last_error(void);

#endif
