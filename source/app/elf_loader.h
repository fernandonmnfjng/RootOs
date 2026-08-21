#ifndef ROOTOS_ELF_LOADER_H
#define ROOTOS_ELF_LOADER_H

#include "types.h"

typedef enum
{
    ROOT_ELF_OK = 0,
    ROOT_ELF_NOT_FOUND,
    ROOT_ELF_IO_ERROR,
    ROOT_ELF_BAD_FORMAT,
    ROOT_ELF_UNSUPPORTED,
    ROOT_ELF_NO_MEMORY,
    ROOT_ELF_NO_ENTRY,
    ROOT_ELF_RELOCATION_ERROR,
    ROOT_ELF_PROCESS_TABLE_FULL
} RootElfResult;

RootElfResult elf_loader_run(
    const char* path,
    const char* process_name,
    int argc,
    const char** argv,
    int* exit_code,
    u32* pid
);

const char* elf_loader_result_string(
    RootElfResult result
);

#endif
