#include "rootfont.h"


typedef struct __attribute__((packed))
{
    u32 magic;

    u32 version;

    u32 glyph_count;

} RootFontHeader;


typedef struct __attribute__((packed))
{
    u32 codepoint;

    u8 width;

    u8 bitmap[32];

} RootFontRecord;


/*
 * Generados por objcopy.
 */

extern const u8
    _binary_rootfont_bin_start[];

extern const u8
    _binary_rootfont_bin_end[];


/*
 * ============================================================
 * VALIDACION
 * ============================================================
 */

static const RootFontHeader*
rootfont_header(void)
{
    usize size =
        (
            usize
        )(
            _binary_rootfont_bin_end
            -
            _binary_rootfont_bin_start
        );


    if (
        size
        <
        sizeof(
            RootFontHeader
        )
    )
    {
        return NULL;
    }


    const RootFontHeader* header =
        (
            const RootFontHeader*
        )
        _binary_rootfont_bin_start;


    if (
        header->magic
        !=
        ROOTFONT_MAGIC
    )
    {
        return NULL;
    }


    if (
        header->version
        !=
        ROOTFONT_VERSION
    )
    {
        return NULL;
    }


    usize expected =
        sizeof(
            RootFontHeader
        )
        +
        (
            usize
        )
        header->glyph_count
        *
        sizeof(
            RootFontRecord
        );


    if (
        expected > size
    )
    {
        return NULL;
    }


    return header;
}


bool rootfont_ready(void)
{
    return
        rootfont_header()
        !=
        NULL;
}


/*
 * ============================================================
 * BUSQUEDA BINARIA
 * ============================================================
 */

static const RootFontRecord*
find_record(
    RootCodepoint codepoint
)
{
    const RootFontHeader* header =
        rootfont_header();


    if (
        header == NULL
    )
    {
        return NULL;
    }


    const RootFontRecord* records =
        (
            const RootFontRecord*
        )(
            _binary_rootfont_bin_start
            +
            sizeof(
                RootFontHeader
            )
        );


    usize left = 0;

    usize right =
        header->glyph_count;


    while (
        left < right
    )
    {
        usize middle =
            left
            +
            (
                right - left
            )
            /
            2;


        RootCodepoint current =
            records[
                middle
            ].codepoint;


        if (
            current
            ==
            codepoint
        )
        {
            return
                &
                records[
                    middle
                ];
        }


        if (
            current
            <
            codepoint
        )
        {
            left =
                middle + 1;
        }

        else
        {
            right =
                middle;
        }
    }


    return NULL;
}


/*
 * ============================================================
 * API
 * ============================================================
 */

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


    const RootFontRecord* record =
        find_record(
            codepoint
        );


    /*
     * Caracter inexistente:
     *
     * U+FFFD
     */
    if (
        record == NULL
        &&
        codepoint
        !=
        ROOT_UNICODE_REPLACEMENT
    )
    {
        record =
            find_record(
                ROOT_UNICODE_REPLACEMENT
            );
    }


    if (
        record == NULL
    )
    {
        return false;
    }


    glyph->width =
        record->width;


    glyph->bitmap =
        record->bitmap;


    return true;
}