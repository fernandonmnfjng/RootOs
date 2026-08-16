#include "string.h"


usize root_strlen(
    const char* text
)
{
    usize length = 0;


    if (text == NULL)
    {
        return 0;
    }


    while (
        text[length]
        !=
        '\0'
    )
    {
        length++;
    }


    return length;
}


int root_strcmp(
    const char* a,
    const char* b
)
{
    while (
        *a
        &&
        *b
        &&
        *a == *b
    )
    {
        a++;
        b++;
    }


    return
        (int)(u8)*a
        -
        (int)(u8)*b;
}


int root_strncmp(
    const char* a,
    const char* b,
    usize count
)
{
    for (
        usize i = 0;
        i < count;
        i++
    )
    {
        u8 left =
            (u8)a[i];

        u8 right =
            (u8)b[i];


        if (
            left != right
        )
        {
            return
                (int)left
                -
                (int)right;
        }


        if (
            left == '\0'
        )
        {
            return 0;
        }
    }


    return 0;
}


bool root_streq(
    const char* a,
    const char* b
)
{
    return
        root_strcmp(a, b)
        ==
        0;
}


bool root_starts_with(
    const char* text,
    const char* prefix
)
{
    while (*prefix)
    {
        if (
            *text
            !=
            *prefix
        )
        {
            return false;
        }


        text++;
        prefix++;
    }


    return true;
}


usize root_strlcpy(
    char* destination,
    const char* source,
    usize destination_size
)
{
    usize source_length =
        root_strlen(source);


    if (
        destination_size
        ==
        0
    )
    {
        return source_length;
    }


    usize copy_length =
        source_length;


    if (
        copy_length
        >=
        destination_size
    )
    {
        copy_length =
            destination_size - 1;
    }


    for (
        usize i = 0;
        i < copy_length;
        i++
    )
    {
        destination[i] =
            source[i];
    }


    destination[
        copy_length
    ] = '\0';


    return source_length;
}


const char* root_strchr(
    const char* text,
    char character
)
{
    while (*text)
    {
        if (
            *text
            ==
            character
        )
        {
            return text;
        }


        text++;
    }


    if (
        character == '\0'
    )
    {
        return text;
    }


    return NULL;
}
