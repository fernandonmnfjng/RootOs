#include "heap.h"

#include "memory.h"


/*
 * ============================================================
 * CONFIGURATION
 * ============================================================
 */

#define HEAP_ALIGNMENT 16u

#define HEAP_MAGIC \
    0x48454150u


/*
 * ============================================================
 * HEAP BLOCK
 * ============================================================
 */

typedef struct HeapBlock
{
    u32 magic;

    usize size;

    bool free;


    struct HeapBlock* previous;

    struct HeapBlock* next;

} HeapBlock;


/*
 * ============================================================
 * HEAP MEMORY
 * ============================================================
 *
 * Por ahora reservamos 8 MiB dentro del BSS.
 *
 * No aumenta el ISO en 8 MiB porque .bss no almacena esos
 * bytes físicamente dentro del ELF.
 */

static u8 heap_memory[
    ROOT_HEAP_SIZE
]
__attribute__((aligned(HEAP_ALIGNMENT)));


static HeapBlock* heap_head =
    NULL;


static bool heap_initialized =
    false;


/*
 * ============================================================
 * ALIGN
 * ============================================================
 */

static usize heap_align(
    usize value
)
{
    usize mask =
        HEAP_ALIGNMENT - 1;


    if (
        value
        >
        ((usize)-1 - mask)
    )
    {
        return 0;
    }


    return
        (
            value
            +
            mask
        )
        &
        ~mask;
}


/*
 * ============================================================
 * VALID BLOCK
 * ============================================================
 */

static bool heap_block_valid(
    const HeapBlock* block
)
{
    if (
        block == NULL
    )
    {
        return false;
    }


    const u8* address =
        (const u8*)block;


    if (
        address
        <
        heap_memory
    )
    {
        return false;
    }


    if (
        address
        >=
        heap_memory
        +
        ROOT_HEAP_SIZE
    )
    {
        return false;
    }


    return
        block->magic
        ==
        HEAP_MAGIC;
}


/*
 * ============================================================
 * SPLIT BLOCK
 * ============================================================
 */

static void heap_split_block(
    HeapBlock* block,
    usize requested_size
)
{
    if (
        !heap_block_valid(
            block
        )
    )
    {
        return;
    }


    /*
     * No dividir si el espacio restante
     * no alcanza para otro bloque útil.
     */

    if (
        block->size
        <=
        requested_size
        +
        sizeof(HeapBlock)
        +
        HEAP_ALIGNMENT
    )
    {
        return;
    }


    u8* new_address =
        (u8*)(block + 1)
        +
        requested_size;


    HeapBlock* new_block =
        (HeapBlock*)new_address;


    new_block->magic =
        HEAP_MAGIC;


    new_block->size =
        block->size
        -
        requested_size
        -
        sizeof(HeapBlock);


    new_block->free =
        true;


    new_block->previous =
        block;


    new_block->next =
        block->next;


    if (
        block->next
        !=
        NULL
    )
    {
        block->next->previous =
            new_block;
    }


    block->next =
        new_block;


    block->size =
        requested_size;
}


/*
 * ============================================================
 * MERGE WITH NEXT
 * ============================================================
 */

static void heap_merge_next(
    HeapBlock* block
)
{
    if (
        !heap_block_valid(
            block
        )
    )
    {
        return;
    }


    HeapBlock* next =
        block->next;


    if (
        next == NULL
        ||
        !heap_block_valid(
            next
        )
        ||
        !next->free
    )
    {
        return;
    }


    block->size +=
        sizeof(HeapBlock)
        +
        next->size;


    block->next =
        next->next;


    if (
        block->next
        !=
        NULL
    )
    {
        block->next->previous =
            block;
    }


    next->magic =
        0;
}


/*
 * ============================================================
 * INITIALIZE
 * ============================================================
 */

void heap_init(void)
{
    heap_head =
        (HeapBlock*)heap_memory;


    heap_head->magic =
        HEAP_MAGIC;


    heap_head->size =
        ROOT_HEAP_SIZE
        -
        sizeof(HeapBlock);


    heap_head->free =
        true;


    heap_head->previous =
        NULL;


    heap_head->next =
        NULL;


    heap_initialized =
        true;
}


/*
 * ============================================================
 * MALLOC
 * ============================================================
 */

void* root_malloc(
    usize size
)
{
    if (
        !heap_initialized
        ||
        size == 0
    )
    {
        return NULL;
    }


    size =
        heap_align(
            size
        );


    if (
        size == 0
    )
    {
        return NULL;
    }


    HeapBlock* block =
        heap_head;


    while (
        block != NULL
    )
    {
        if (
            block->free
            &&
            block->size
            >=
            size
        )
        {
            heap_split_block(
                block,
                size
            );


            block->free =
                false;


            return
                (void*)(
                    block + 1
                );
        }


        block =
            block->next;
    }


    return NULL;
}


/*
 * ============================================================
 * CALLOC
 * ============================================================
 */

void* root_calloc(
    usize count,
    usize size
)
{
    if (
        count == 0
        ||
        size == 0
    )
    {
        return NULL;
    }


    /*
     * Comprobar overflow.
     */

    if (
        size
        >
        ((usize)-1)
        /
        count
    )
    {
        return NULL;
    }


    usize total =
        count
        *
        size;


    void* pointer =
        root_malloc(
            total
        );


    if (
        pointer == NULL
    )
    {
        return NULL;
    }


    root_memzero(
        pointer,
        total
    );


    return pointer;
}


/*
 * ============================================================
 * POINTER OWNERSHIP
 * ============================================================
 */

bool heap_owns(
    const void* pointer
)
{
    if (
        pointer == NULL
        ||
        !heap_initialized
    )
    {
        return false;
    }


    const u8* address =
        (const u8*)pointer;


    return
        address
        >
        heap_memory
        &&
        address
        <
        heap_memory
        +
        ROOT_HEAP_SIZE;
}


/*
 * ============================================================
 * FREE
 * ============================================================
 */

void root_free(
    void* pointer
)
{
    if (
        pointer == NULL
        ||
        !heap_owns(
            pointer
        )
    )
    {
        return;
    }


    HeapBlock* block =
        (
            (HeapBlock*)pointer
        )
        -
        1;


    if (
        !heap_block_valid(
            block
        )
        ||
        block->free
    )
    {
        return;
    }


    block->free =
        true;


    /*
     * Unir con siguiente bloque.
     */

    heap_merge_next(
        block
    );


    /*
     * Unir con anterior.
     */

    if (
        block->previous
        !=
        NULL
        &&
        block->previous->free
    )
    {
        heap_merge_next(
            block->previous
        );
    }
}


/*
 * ============================================================
 * REALLOC
 * ============================================================
 */

void* root_realloc(
    void* pointer,
    usize new_size
)
{
    if (
        pointer == NULL
    )
    {
        return
            root_malloc(
                new_size
            );
    }


    if (
        new_size == 0
    )
    {
        root_free(
            pointer
        );


        return NULL;
    }


    if (
        !heap_owns(
            pointer
        )
    )
    {
        return NULL;
    }


    HeapBlock* block =
        (
            (HeapBlock*)pointer
        )
        -
        1;


    if (
        !heap_block_valid(
            block
        )
        ||
        block->free
    )
    {
        return NULL;
    }


    usize aligned_size =
        heap_align(
            new_size
        );


    if (
        aligned_size == 0
    )
    {
        return NULL;
    }


    /*
     * Ya cabe.
     */

    if (
        aligned_size
        <=
        block->size
    )
    {
        heap_split_block(
            block,
            aligned_size
        );


        return pointer;
    }


    /*
     * Intentar crecer usando el siguiente
     * bloque si está libre.
     */

    if (
        block->next
        !=
        NULL
        &&
        block->next->free
        &&
        block->size
        +
        sizeof(HeapBlock)
        +
        block->next->size
        >=
        aligned_size
    )
    {
        heap_merge_next(
            block
        );


        heap_split_block(
            block,
            aligned_size
        );


        block->free =
            false;


        return pointer;
    }


    /*
     * Necesitamos mover el bloque.
     */

    void* new_pointer =
        root_malloc(
            new_size
        );


    if (
        new_pointer == NULL
    )
    {
        return NULL;
    }


    usize copy_size =
        block->size;


    if (
        copy_size
        >
        new_size
    )
    {
        copy_size =
            new_size;
    }


    root_memcpy(
        new_pointer,
        pointer,
        copy_size
    );


    root_free(
        pointer
    );


    return new_pointer;
}


/*
 * ============================================================
 * STATISTICS
 * ============================================================
 */

usize heap_total_bytes(void)
{
    if (
        !heap_initialized
    )
    {
        return 0;
    }


    return
        ROOT_HEAP_SIZE
        -
        sizeof(HeapBlock);
}


usize heap_used_bytes(void)
{
    if (
        !heap_initialized
    )
    {
        return 0;
    }


    usize used =
        0;


    HeapBlock* block =
        heap_head;


    while (
        block != NULL
    )
    {
        if (
            !block->free
        )
        {
            used +=
                block->size;
        }


        block =
            block->next;
    }


    return used;
}


usize heap_free_bytes(void)
{
    if (
        !heap_initialized
    )
    {
        return 0;
    }


    usize free_bytes =
        0;


    HeapBlock* block =
        heap_head;


    while (
        block != NULL
    )
    {
        if (
            block->free
        )
        {
            free_bytes +=
                block->size;
        }


        block =
            block->next;
    }


    return free_bytes;
}