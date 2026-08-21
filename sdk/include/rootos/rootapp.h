#ifndef ROOTOS_SDK_ROOTAPP_H
#define ROOTOS_SDK_ROOTAPP_H

/* RootOS Application ABI v1 - x86 32-bit */

typedef unsigned char root_u8;
typedef unsigned short root_u16;
typedef unsigned int root_u32;
typedef unsigned long root_usize;
typedef int root_bool;

#define ROOT_APP_ABI_VERSION 1u

typedef struct RootApi
{
    root_u32 abi_version;

    void (*print)(const char* text);
    void (*putchar)(char c);

    void* (*alloc)(root_usize size);
    void (*free)(void* pointer);

    int (*read_file)(
        const char* path,
        void* output,
        root_usize capacity,
        root_usize* result_size
    );

    int (*write_file)(
        const char* path,
        const void* data,
        root_usize size
    );

    int (*append_file)(
        const char* path,
        const void* data,
        root_usize size
    );

    root_bool (*file_exists)(const char* path);

    const char* (*os_version)(void);
} RootApi;

/* Every v0.42 application exports exactly this symbol. */
int root_main(
    const RootApi* api,
    int argc,
    const char** argv
);

#endif
