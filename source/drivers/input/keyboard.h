#ifndef ROOTOS_KEYBOARD_H
#define ROOTOS_KEYBOARD_H

#include "types.h"
#include "unicode.h"
#include "keycodes.h"


typedef struct
{
    RootKey key;

    RootCodepoint codepoint;

    bool pressed;

    bool shift;
    bool ctrl;
    bool alt;
    bool altgr;

    bool caps_lock;
    bool num_lock;

} KeyboardEvent;


void keyboard_reset(void);


bool keyboard_feed_byte(
    u8 scancode,
    KeyboardEvent* event
);


#endif