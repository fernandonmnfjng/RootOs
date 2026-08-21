#ifndef ROOTOS_FILESYSTEM_H
#define ROOTOS_FILESYSTEM_H

#include "types.h"


/*
 * ============================================================
 * FILESYSTEM LIMITS
 * ============================================================
 */

#define FS_MAX_FILE_SIZE (64u * 1024u)


/*
 * ============================================================
 * RESULTS
 * ============================================================
 */

typedef enum
{
    FS_RESULT_OK = 1,

    FS_RESULT_NOT_FOUND = -1,

    FS_RESULT_ALREADY_EXISTS = -2,

    FS_RESULT_NOT_DIRECTORY = -3,

    FS_RESULT_DIRECTORY_NOT_EMPTY = -4,

    FS_RESULT_INVALID_PATH = -5,

    FS_RESULT_NO_SPACE = -6,

    FS_RESULT_BUSY = -7,

    FS_RESULT_NOT_FILE = -8,

    FS_RESULT_FILE_TOO_LARGE = -9,

    FS_RESULT_IO_ERROR = -10

} FsResult;


/*
 * ============================================================
 * INITIALIZATION
 * ============================================================
 */

void filesystem_init(void);


/*
 * ============================================================
 * BACKEND INFORMATION
 * ============================================================
 */

bool filesystem_is_persistent(void);

bool filesystem_storage_faulted(void);

usize filesystem_node_count(void);

usize filesystem_free_node_count(void);


/*
 * ============================================================
 * NAVIGATION
 * ============================================================
 */

int filesystem_change_directory(
    const char* path
);


void filesystem_print_current_directory(void);


const char* filesystem_current_directory(void);


/*
 * ============================================================
 * LISTING
 * ============================================================
 */

int filesystem_list(
    const char* path
);


int filesystem_find_directories(
    const char* name
);


/*
 * ============================================================
 * CREATE
 * ============================================================
 */

FsResult filesystem_create_file(
    const char* path
);


FsResult filesystem_create_directory(
    const char* path
);


/*
 * ============================================================
 * REMOVE
 * ============================================================
 */

FsResult filesystem_remove(
    const char* path,
    bool recursive
);


/*
 * ============================================================
 * COPY / MOVE
 * ============================================================
 */

FsResult filesystem_copy(
    const char* source,
    const char* destination
);


FsResult filesystem_move(
    const char* source,
    const char* destination
);


/*
 * ============================================================
 * FILE CONTENT
 * ============================================================
 */

FsResult filesystem_read_file(
    const char* path,
    char* output,
    usize output_size,
    usize* result_size
);


FsResult filesystem_write_file(
    const char* path,
    const char* data,
    usize size
);


FsResult filesystem_append_file(
    const char* path,
    const char* data,
    usize size
);


/*
 * ============================================================
 * INFORMATION
 * ============================================================
 */

bool filesystem_exists(
    const char* path
);


bool filesystem_is_directory(
    const char* path
);


bool filesystem_file_size(
    const char* path,
    usize* size
);


#endif
