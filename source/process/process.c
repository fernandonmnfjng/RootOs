#include "process.h"

#include "memory.h"
#include "string.h"

static RootProcessInfo process_table[ROOT_PROCESS_MAX];
static u32 next_pid = 1u;
static u32 replacement_cursor = 0u;

static RootProcessInfo* process_find(u32 pid)
{
    for (u32 i = 0; i < ROOT_PROCESS_MAX; i++)
    {
        if (
            process_table[i].used
            &&
            process_table[i].pid == pid
        )
        {
            return &process_table[i];
        }
    }

    return NULL;
}

static u32 process_choose_slot(void)
{
    for (u32 i = 0; i < ROOT_PROCESS_MAX; i++)
    {
        if (!process_table[i].used)
        {
            return i;
        }
    }

    /*
     * v0.42 processes are synchronous. Once the history table fills, reuse an
     * exited/failed slot in round-robin order rather than growing memory.
     */
    for (u32 attempt = 0; attempt < ROOT_PROCESS_MAX; attempt++)
    {
        u32 index =
            (replacement_cursor + attempt)
            %
            ROOT_PROCESS_MAX;

        if (
            process_table[index].state == ROOT_PROCESS_EXITED
            ||
            process_table[index].state == ROOT_PROCESS_FAILED
        )
        {
            replacement_cursor =
                (index + 1u)
                %
                ROOT_PROCESS_MAX;

            return index;
        }
    }

    /* No reusable slot. This should only happen after real concurrency exists. */
    return ROOT_PROCESS_MAX;
}

void process_manager_init(void)
{
    root_memzero(
        process_table,
        sizeof(process_table)
    );

    next_pid = 1u;
    replacement_cursor = 0u;
}

u32 process_begin(
    const char* name,
    usize image_size
)
{
    u32 index = process_choose_slot();

    if (index >= ROOT_PROCESS_MAX)
    {
        return 0;
    }

    RootProcessInfo* process = &process_table[index];

    root_memzero(
        process,
        sizeof(*process)
    );

    process->used = true;
    process->pid = next_pid++;

    if (next_pid == 0u)
    {
        next_pid = 1u;
    }

    process->state = ROOT_PROCESS_READY;
    process->exit_code = 0;
    process->image_size = image_size;

    if (name == NULL || name[0] == '\0')
    {
        name = "application";
    }

    root_strlcpy(
        process->name,
        name,
        sizeof(process->name)
    );

    return process->pid;
}

void process_mark_running(u32 pid)
{
    RootProcessInfo* process = process_find(pid);

    if (process != NULL)
    {
        process->state = ROOT_PROCESS_RUNNING;
    }
}

void process_mark_exit(u32 pid, int exit_code)
{
    RootProcessInfo* process = process_find(pid);

    if (process != NULL)
    {
        process->state = ROOT_PROCESS_EXITED;
        process->exit_code = exit_code;
    }
}

void process_mark_failed(u32 pid, int exit_code)
{
    RootProcessInfo* process = process_find(pid);

    if (process != NULL)
    {
        process->state = ROOT_PROCESS_FAILED;
        process->exit_code = exit_code;
    }
}

usize process_count(void)
{
    usize count = 0;

    for (u32 i = 0; i < ROOT_PROCESS_MAX; i++)
    {
        if (process_table[i].used)
        {
            count++;
        }
    }

    return count;
}

bool process_get(
    usize index,
    RootProcessInfo* output
)
{
    if (output == NULL)
    {
        return false;
    }

    usize visible = 0;

    for (u32 i = 0; i < ROOT_PROCESS_MAX; i++)
    {
        if (!process_table[i].used)
        {
            continue;
        }

        if (visible == index)
        {
            *output = process_table[i];
            return true;
        }

        visible++;
    }

    return false;
}

const char* process_state_name(
    RootProcessState state
)
{
    switch (state)
    {
        case ROOT_PROCESS_READY:
            return "ready";

        case ROOT_PROCESS_RUNNING:
            return "running";

        case ROOT_PROCESS_EXITED:
            return "exited";

        case ROOT_PROCESS_FAILED:
            return "failed";

        case ROOT_PROCESS_UNUSED:
        default:
            return "unused";
    }
}
