#include "unicode.h"


bool root_unicode_valid(
    RootCodepoint codepoint
)
{
    /*
     * Unicode termina en U+10FFFF.
     */
    if (
        codepoint
        >
        0x10FFFF
    )
    {
        return false;
    }


    /*
     * Surrogates UTF-16.
     *
     * No son codepoints válidos
     * independientes.
     */
    if (
        codepoint >= 0xD800
        &&
        codepoint <= 0xDFFF
    )
    {
        return false;
    }


    return true;
}


usize root_utf8_encode(
    RootCodepoint codepoint,
    char output[4]
)
{
    if (
        !root_unicode_valid(
            codepoint
        )
    )
    {
        codepoint =
            ROOT_UNICODE_REPLACEMENT;
    }


    if (
        codepoint <= 0x7F
    )
    {
        output[0] =
            (char)codepoint;

        return 1;
    }


    if (
        codepoint <= 0x7FF
    )
    {
        output[0] =
            (char)(
                0xC0
                |
                (
                    codepoint
                    >>
                    6
                )
            );


        output[1] =
            (char)(
                0x80
                |
                (
                    codepoint
                    &
                    0x3F
                )
            );


        return 2;
    }


    if (
        codepoint <= 0xFFFF
    )
    {
        output[0] =
            (char)(
                0xE0
                |
                (
                    codepoint
                    >>
                    12
                )
            );


        output[1] =
            (char)(
                0x80
                |
                (
                    (
                        codepoint
                        >>
                        6
                    )
                    &
                    0x3F
                )
            );


        output[2] =
            (char)(
                0x80
                |
                (
                    codepoint
                    &
                    0x3F
                )
            );


        return 3;
    }


    output[0] =
        (char)(
            0xF0
            |
            (
                codepoint
                >>
                18
            )
        );


    output[1] =
        (char)(
            0x80
            |
            (
                (
                    codepoint
                    >>
                    12
                )
                &
                0x3F
            )
        );


    output[2] =
        (char)(
            0x80
            |
            (
                (
                    codepoint
                    >>
                    6
                )
                &
                0x3F
            )
        );


    output[3] =
        (char)(
            0x80
            |
            (
                codepoint
                &
                0x3F
            )
        );


    return 4;
}


usize root_utf8_decode(
    const char* input,
    usize available,
    RootCodepoint* output
)
{
    if (
        input == NULL
        ||
        output == NULL
        ||
        available == 0
    )
    {
        return 0;
    }


    const u8* bytes =
        (const u8*)input;


    /*
     * ASCII.
     */
    if (
        bytes[0] <= 0x7F
    )
    {
        *output =
            bytes[0];

        return 1;
    }


    /*
     * 2 bytes.
     */
    if (
        (bytes[0] & 0xE0)
        ==
        0xC0
    )
    {
        if (available < 2)
        {
            return 0;
        }


        if (
            (bytes[1] & 0xC0)
            !=
            0x80
        )
        {
            return 0;
        }


        RootCodepoint cp =
            (
                (
                    bytes[0]
                    &
                    0x1F
                )
                <<
                6
            )
            |
            (
                bytes[1]
                &
                0x3F
            );


        if (cp < 0x80)
        {
            return 0;
        }


        *output = cp;

        return 2;
    }


    /*
     * 3 bytes.
     */
    if (
        (bytes[0] & 0xF0)
        ==
        0xE0
    )
    {
        if (available < 3)
        {
            return 0;
        }


        if (
            (bytes[1] & 0xC0)
                !=
                0x80
            ||
            (bytes[2] & 0xC0)
                !=
                0x80
        )
        {
            return 0;
        }


        RootCodepoint cp =
            (
                (
                    bytes[0]
                    &
                    0x0F
                )
                <<
                12
            )
            |
            (
                (
                    bytes[1]
                    &
                    0x3F
                )
                <<
                6
            )
            |
            (
                bytes[2]
                &
                0x3F
            );


        if (
            cp < 0x800
            ||
            !root_unicode_valid(cp)
        )
        {
            return 0;
        }


        *output = cp;

        return 3;
    }


    /*
     * 4 bytes.
     */
    if (
        (bytes[0] & 0xF8)
        ==
        0xF0
    )
    {
        if (available < 4)
        {
            return 0;
        }


        if (
            (bytes[1] & 0xC0)
                !=
                0x80
            ||
            (bytes[2] & 0xC0)
                !=
                0x80
            ||
            (bytes[3] & 0xC0)
                !=
                0x80
        )
        {
            return 0;
        }


        RootCodepoint cp =
            (
                (
                    bytes[0]
                    &
                    0x07
                )
                <<
                18
            )
            |
            (
                (
                    bytes[1]
                    &
                    0x3F
                )
                <<
                12
            )
            |
            (
                (
                    bytes[2]
                    &
                    0x3F
                )
                <<
                6
            )
            |
            (
                bytes[3]
                &
                0x3F
            );


        if (
            cp < 0x10000
            ||
            !root_unicode_valid(cp)
        )
        {
            return 0;
        }


        *output = cp;

        return 4;
    }


    return 0;
}
