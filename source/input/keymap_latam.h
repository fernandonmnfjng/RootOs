#ifndef ROOTOS_KEYMAP_LATAM_H
#define ROOTOS_KEYMAP_LATAM_H

#include "types.h"
#include "unicode.h"
#include "keycodes.h"


RootCodepoint keymap_latam_translate(
    RootKey key,
    bool shift,
    bool altgr,
    bool caps_lock,
    bool num_lock
);


void keymap_latam_reset(void);


#endif