#ifndef ROOTOS_KEYMAP_LATAM_H
#define ROOTOS_KEYMAP_LATAM_H

#include "io.h"


u8 keymap_latam_translate(
    u8 scancode,
    int shift,
    int altgr,
    int caps_lock,
    int num_lock
);


void keymap_latam_reset(void);


#endif
