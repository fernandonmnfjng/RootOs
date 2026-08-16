#ifndef ROOTOS_PATH_H
#define ROOTOS_PATH_H

#include "types.h"
#include "system_config.h"

#define ROOT_PATH_MAX 512
#define ROOT_NAME_MAX 128

bool root_path_resolve(
    const char* current_directory,
    const char* input,
    char* output,
    usize output_size
);


const char* root_path_basename(
    const char* path
);


bool root_path_is_direct_child(
    const char* parent,
    const char* child
);


#endif
