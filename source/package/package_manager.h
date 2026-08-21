#ifndef ROOTOS_PACKAGE_MANAGER_H
#define ROOTOS_PACKAGE_MANAGER_H

#include "types.h"
#include "rtpgk.h"

#define ROOT_PACKAGE_MAX_INSTALLED 64u

typedef struct
{
    char name[RTPGK_NAME_MAX];
    char version[RTPGK_VERSION_TEXT_MAX];
    char entry[RTPGK_ENTRY_NAME_MAX];
} RootPackageInfo;

typedef enum
{
    ROOT_PACKAGE_OK = 0,
    ROOT_PACKAGE_NOT_FOUND,
    ROOT_PACKAGE_ALREADY_INSTALLED,
    ROOT_PACKAGE_INVALID,
    ROOT_PACKAGE_IO_ERROR,
    ROOT_PACKAGE_NO_MEMORY,
    ROOT_PACKAGE_NO_SPACE,
    ROOT_PACKAGE_NOT_INSTALLED
} RootPackageResult;

void package_manager_init(void);

RootPackageResult package_install(
    const char* package_path
);

RootPackageResult package_remove(
    const char* name
);

usize package_count(void);

bool package_get(
    usize index,
    RootPackageInfo* output
);

bool package_find(
    const char* name,
    RootPackageInfo* output
);

const char* package_result_string(
    RootPackageResult result
);

#endif
