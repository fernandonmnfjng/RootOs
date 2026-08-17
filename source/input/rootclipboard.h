#ifndef ROOTOS_ROOTCLIPBOARD_H
#define ROOTOS_ROOTCLIPBOARD_H

#include "types.h"
#include "unicode.h"

#define ROOT_CLIPBOARD_MAX_CODEPOINTS 4096

void rootclipboard_init(void);
void rootclipboard_clear(void);

bool rootclipboard_set(
    const RootCodepoint* text,
    usize length
);

usize rootclipboard_get(
    RootCodepoint* output,
    usize capacity
);

const RootCodepoint* rootclipboard_data(void);
usize rootclipboard_length(void);
bool rootclipboard_empty(void);

#endif
