#include "rootfont.h"


/*
 * Estos símbolos serán creados por objcopy
 * a partir de build/rootfont.bin.
 */

extern const u8
    _binary_rootfont_bin_start[];

extern const u8
    _binary_rootfont_bin_end[];


bool rootfont_get_glyph(
    RootCodepoint codepoint,
    RootGlyph* glyph
)
{
    if (
        glyph == NULL
    )
    {
        return false;
    }


    /*
     * Primera versión:
     *
     * Basic Multilingual Plane.
     */
    if (
        codepoint
        >=
        ROOTFONT_BMP_COUNT
    )
    {
        codepoint =
            ROOT_UNICODE_REPLACEMENT;
    }


    usize offset =
        (
            usize
        )
        codepoint
        *
        ROOTFONT_RECORD_SIZE;


    const u8* record =
        _binary_rootfont_bin_start
        +
        offset;


    u8 width =
        record[0];


    /*
     * No existe glifo.
     */
    if (
        width != 8
        &&
        width != 16
    )
    {
        /*
         * Mostrar U+FFFD.
         */

        offset =
            (
                usize
            )
            ROOT_UNICODE_REPLACEMENT
            *
            ROOTFONT_RECORD_SIZE;


        record =
            _binary_rootfont_bin_start
            +
            offset;


        width =
            record[0];


        if (
            width != 8
            &&
            width != 16
        )
        {
            return false;
        }
    }


    glyph->width =
        width;


    glyph->bitmap =
        record + 1;


    return true;
}
