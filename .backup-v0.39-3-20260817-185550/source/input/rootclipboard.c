#include "rootclipboard.h"


/*
 * ============================================================
 * STORAGE
 * ============================================================
 */

static RootCodepoint clipboard_data[
    ROOT_CLIPBOARD_MAX_CODEPOINTS
];


static usize clipboard_length =
    0;


/*
 * ============================================================
 * INITIALIZATION
 * ============================================================
 */

void rootclipboard_init(void)
{
    clipboard_length =
        0;


    clipboard_data[0] =
        0;
}


/*
 * ============================================================
 * CLEAR
 * ============================================================
 */

void rootclipboard_clear(void)
{
    clipboard_length =
        0;


    clipboard_data[0] =
        0;
}


/*
 * ============================================================
 * SET
 * ============================================================
 */

bool rootclipboard_set(
    const RootCodepoint* text,
    usize length
)
{
    if (
        text == NULL
        &&
        length != 0
    )
    {
        return false;
    }


    if (
        length
        >=
        ROOT_CLIPBOARD_MAX_CODEPOINTS
    )
    {
        return false;
    }


    for (
        usize i = 0;
        i < length;
        i++
    )
    {
        clipboard_data[i] =
            text[i];
    }


    clipboard_length =
        length;


    clipboard_data[
        clipboard_length
    ] =
        0;


    return true;
}


/*
 * ============================================================
 * GET
 * ============================================================
 */

usize rootclipboard_get(
    RootCodepoint* output,
    usize capacity
)
{
    if (
        output == NULL
        ||
        capacity == 0
    )
    {
        return 0;
    }


    usize copy_length =
        clipboard_length;


    if (
        copy_length
        >=
        capacity
    )
    {
        copy_length =
            capacity - 1;
    }


    for (
        usize i = 0;
        i < copy_length;
        i++
    )
    {
        output[i] =
            clipboard_data[i];
    }


    output[
        copy_length
    ] =
        0;


    return
        copy_length;
}


/*
 * ============================================================
 * DIRECT ACCESS
 * ============================================================
 */

const RootCodepoint* rootclipboard_data(void)
{
    return
        clipboard_data;
}


usize rootclipboard_length(void)
{
    return
        clipboard_length;
}


bool rootclipboard_empty(void)
{
    return
        clipboard_length
        ==
        0;
}