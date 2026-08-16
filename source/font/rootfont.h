#ifndef ROOTOS_ROOTFONT_H
#define ROOTOS_ROOTFONT_H

#include "types.h"
#include "unicode.h"


#define ROOTFONT_HEIGHT 16

#define ROOTFONT_MAX_WIDTH 16

#define ROOTFONT_RECORD_SIZE 33

#define ROOTFONT_BMP_COUNT 0x10000


typedef struct
{
    u8 width;

    /*
     * Máximo:
     *
     * 16 pixels
     * x
     * 16 filas
     *
     * =
     *
     * 2 bytes * 16
     */
    const u8* bitmap;

} RootGlyph;


bool rootfont_get_glyph(
    RootCodepoint codepoint,
    RootGlyph* glyph
);


#endif
