#ifndef ROOTOS_FILESYSTEM_H
#define ROOTOS_FILESYSTEM_H


void filesystem_init(void);


int filesystem_change_directory(
    const char* path
);


void filesystem_print_current_directory(void);


int filesystem_list(
    const char* path
);


int filesystem_find_directories(
    const char* name
);


const char* filesystem_current_directory(void);


#endif