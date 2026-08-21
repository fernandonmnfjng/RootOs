#ifndef ROOTOS_APP_MANAGER_H
#define ROOTOS_APP_MANAGER_H

#include "types.h"
#include "elf_loader.h"

#define ROOT_APP_NAME_MAX 64u
#define ROOT_APP_MANIFEST_MAX 2048u

typedef enum
{
    ROOT_APP_RUN_OK = 0,
    ROOT_APP_RUN_NOT_FOUND,
    ROOT_APP_RUN_BAD_MANIFEST,
    ROOT_APP_RUN_INVALID_NAME,
    ROOT_APP_RUN_ELF_ERROR
} RootAppRunResult;

typedef struct
{
    RootAppRunResult result;
    RootElfResult elf_result;
    int exit_code;
    u32 pid;
    char executable[512];
} RootAppRunInfo;

void app_manager_init(void);

RootAppRunInfo app_manager_run(
    const char* target,
    int argc,
    const char** argv
);

#endif
