#ifndef ROOTOS_ROOTFONT_H
#define ROOTOS_ROOTFONT_H

#include "types.h"
#include "unicode.h"


#define ROOTFONT_HEIGHT 16

#define ROOTFONT_MAX_WIDTH 16

#define ROOTFONT_MAGIC \
    0x544E4652

#define ROOTFONT_VERSION \
    1


typedef struct
{
    u8 width;

    const u8* bitmap;

} RootGlyph;


bool rootfont_ready(void);


bool rootfont_get_glyph(
    RootCodepoint codepoint,
    RootGlyph* glyph
);


#endif