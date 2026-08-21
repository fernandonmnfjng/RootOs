#ifndef ROOTOS_ELF_MODULE_H
#define ROOTOS_ELF_MODULE_H

#include "types.h"

typedef enum
{
    ROOT_MODULE_OK = 0,
    ROOT_MODULE_BAD_FORMAT,
    ROOT_MODULE_UNSUPPORTED,
    ROOT_MODULE_NO_MEMORY,
    ROOT_MODULE_NO_ENTRY,
    ROOT_MODULE_RELOCATION_ERROR
} RootModuleResult;

typedef struct
{
    void* image_allocation;
    void* image_base;
    usize image_size;
    void* entry;
} RootLoadedModule;

RootModuleResult elf_module_load_memory(
    const void* data,
    usize size,
    const char* entry_symbol,
    RootLoadedModule* output
);

const char* elf_module_result_string(RootModuleResult result);

#endif
