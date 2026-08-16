#include "memory.h"

void* root_memset(
    void* destination,
    int value,
    usize count
)
{
    u8* dst = (u8*)destination;

    for (usize i = 0; i < count; i++)
    {
        dst[i] = (u8)value;
    }

    return destination;
}


void* root_memcpy(
    void* destination,
    const void* source,
    usize count
)
{
    u8* dst = (u8*)destination;
    const u8* src = (const u8*)source;

    for (usize i = 0; i < count; i++)
    {
        dst[i] = src[i];
    }

    return destination;
}


void* root_memmove(
    void* destination,
    const void* source,
    usize count
)
{
    u8* dst = (u8*)destination;
    const u8* src = (const u8*)source;

    if (dst < src || dst >= src + count)
    {
        for (usize i = 0; i < count; i++)
        {
            dst[i] = src[i];
        }
    }
    else
    {
        for (usize i = count; i > 0; i--)
        {
            dst[i - 1] = src[i - 1];
        }
    }

    return destination;
}


int root_memcmp(
    const void* a,
    const void* b,
    usize count
)
{
    const u8* left = (const u8*)a;
    const u8* right = (const u8*)b;

    for (usize i = 0; i < count; i++)
    {
        if (left[i] < right[i])
        {
            return -1;
        }

        if (left[i] > right[i])
        {
            return 1;
        }
    }

    return 0;
}


void root_memzero(
    void* destination,
    usize count
)
{
    root_memset(
        destination,
        0,
        count
    );
}