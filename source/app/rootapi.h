#ifndef ROOTOS_ROOTAPI_H
#define ROOTOS_ROOTAPI_H

#include "types.h"

#define ROOT_APP_ABI_VERSION 1u

typedef struct RootApi
{
    u32 abi_version;

    void (*print)(const char* text);
    void (*putchar)(char c);

    void* (*alloc)(usize size);
    void (*free)(void* pointer);

    int (*read_file)(
        const char* path,
        void* output,
        usize capacity,
        usize* result_size
    );

    int (*write_file)(
        const char* path,
        const void* data,
        usize size
    );

    int (*append_file)(
        const char* path,
        const void* data,
        usize size
    );

    bool (*file_exists)(const char* path);

    const char* (*os_version)(void);
} RootApi;

typedef int (*RootAppEntry)(
    const RootApi* api,
    int argc,
    const char** argv
);

void rootapi_init(void);
const RootApi* rootapi_get(void);

#endif
