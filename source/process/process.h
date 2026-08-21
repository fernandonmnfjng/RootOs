#ifndef ROOTOS_PROCESS_H
#define ROOTOS_PROCESS_H

#include "types.h"

#define ROOT_PROCESS_MAX 32u
#define ROOT_PROCESS_NAME_MAX 64u

typedef enum
{
    ROOT_PROCESS_UNUSED = 0,
    ROOT_PROCESS_READY,
    ROOT_PROCESS_RUNNING,
    ROOT_PROCESS_EXITED,
    ROOT_PROCESS_FAILED
} RootProcessState;

typedef struct
{
    bool used;
    u32 pid;
    RootProcessState state;
    int exit_code;
    usize image_size;
    char name[ROOT_PROCESS_NAME_MAX];
} RootProcessInfo;

void process_manager_init(void);

u32 process_begin(
    const char* name,
    usize image_size
);

void process_mark_running(u32 pid);
void process_mark_exit(u32 pid, int exit_code);
void process_mark_failed(u32 pid, int exit_code);

usize process_count(void);

bool process_get(
    usize index,
    RootProcessInfo* output
);

const char* process_state_name(
    RootProcessState state
);

#endif
