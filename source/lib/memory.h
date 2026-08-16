#ifndef ROOTOS_MEMORY_H
#define ROOTOS_MEMORY_H

#include "types.h"

void* root_memset(
    void* destination,
    int value,
    usize count
);

void* root_memcpy(
    void* destination,
    const void* source,
    usize count
);

void* root_memmove(
    void* destination,
    const void* source,
    usize count
);

int root_memcmp(
    const void* a,
    const void* b,
    usize count
);

void root_memzero(
    void* destination,
    usize count
);

#endif