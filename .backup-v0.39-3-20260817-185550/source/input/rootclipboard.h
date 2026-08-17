#ifndef ROOTOS_ROOTCLIPBOARD_H
#define ROOTOS_ROOTCLIPBOARD_H

#include "types.h"
#include "unicode.h"


/*
 * ============================================================
 * ROOTOS GLOBAL CLIPBOARD
 * ============================================================
 *
 * Clipboard compartido entre:
 *
 * Terminal
 * RootEdit
 * futuras aplicaciones GUI
 *
 * Almacenamos Unicode como RootCodepoint para evitar
 * conversiones UTF-8 innecesarias internamente.
 */

#define ROOT_CLIPBOARD_MAX_CODEPOINTS 4096


/*
 * ============================================================
 * INITIALIZATION
 * ============================================================
 */

void rootclipboard_init(void);


/*
 * ============================================================
 * CLEAR
 * ============================================================
 */

void rootclipboard_clear(void);


/*
 * ============================================================
 * SET
 * ============================================================
 */

bool rootclipboard_set(
    const RootCodepoint* text,
    usize length
);


/*
 * ============================================================
 * GET
 * ============================================================
 */

usize rootclipboard_get(
    RootCodepoint* output,
    usize capacity
);


/*
 * ============================================================
 * DIRECT ACCESS
 * ============================================================
 */

const RootCodepoint* rootclipboard_data(void);


usize rootclipboard_length(void);


bool rootclipboard_empty(void);


#endif