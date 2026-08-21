#include "rootapi.h"

#include "terminal.h"
#include "heap.h"
#include "filesystem.h"
#include "system_config.h"

static RootApi root_api;

static int api_read_file(
    const char* path,
    void* output,
    usize capacity,
    usize* result_size
)
{
    return (int)filesystem_read_file(
        path,
        (char*)output,
        capacity,
        result_size
    );
}

static int api_write_file(
    const char* path,
    const void* data,
    usize size
)
{
    return (int)filesystem_write_file(
        path,
        (const char*)data,
        size
    );
}

static int api_append_file(
    const char* path,
    const void* data,
    usize size
)
{
    return (int)filesystem_append_file(
        path,
        (const char*)data,
        size
    );
}

static const char* api_os_version(void)
{
    return ROOTOS_VERSION_STRING;
}

void rootapi_init(void)
{
    root_api.abi_version = ROOT_APP_ABI_VERSION;
    root_api.print = terminal_print;
    root_api.putchar = terminal_putchar;
    root_api.alloc = root_malloc;
    root_api.free = root_free;
    root_api.read_file = api_read_file;
    root_api.write_file = api_write_file;
    root_api.append_file = api_append_file;
    root_api.file_exists = filesystem_exists;
    root_api.os_version = api_os_version;
}

const RootApi* rootapi_get(void)
{
    return &root_api;
}
