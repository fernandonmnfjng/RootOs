#ifndef ROOTOS_STRING_H
#define ROOTOS_STRING_H

#include "types.h"


usize root_strlen(
    const char* text
);


int root_strcmp(
    const char* a,
    const char* b
);


int root_strncmp(
    const char* a,
    const char* b,
    usize count
);


bool root_streq(
    const char* a,
    const char* b
);


bool root_starts_with(
    const char* text,
    const char* prefix
);


usize root_strlcpy(
    char* destination,
    const char* source,
    usize destination_size
);


const char* root_strchr(
    const char* text,
    char character
);


#endif
