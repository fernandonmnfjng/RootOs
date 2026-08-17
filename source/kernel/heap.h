#ifndef ROOTOS_HEAP_H
#define ROOTOS_HEAP_H

#include "types.h"


/*
 * ============================================================
 * ROOTOS HEAP
 * ============================================================
 *
 * Heap temporal para la etapa x86 de RootOS.
 *
 * Más adelante será reemplazado por:
 *
 * physical memory manager
 *        ↓
 * virtual memory manager
 *        ↓
 * kernel heap
 */

#define ROOT_HEAP_SIZE \
    (8u * 1024u * 1024u)


void heap_init(void);


void* root_malloc(
    usize size
);


void* root_calloc(
    usize count,
    usize size
);


void* root_realloc(
    void* pointer,
    usize new_size
);


void root_free(
    void* pointer
);


bool heap_owns(
    const void* pointer
);


usize heap_total_bytes(void);

usize heap_used_bytes(void);

usize heap_free_bytes(void);


#endif