#ifndef ROOTOS_ROOTINPUT_H
#define ROOTOS_ROOTINPUT_H

#include "types.h"
#include "unicode.h"
#include "keycodes.h"


/*
 * ============================================================
 * INPUT EVENT TYPES
 * ============================================================
 */

typedef enum
{
    ROOT_INPUT_NONE = 0,

    ROOT_INPUT_KEY_DOWN,
    ROOT_INPUT_KEY_UP,

    ROOT_INPUT_MOUSE_MOVE,

    ROOT_INPUT_MOUSE_BUTTON_DOWN,
    ROOT_INPUT_MOUSE_BUTTON_UP,

    ROOT_INPUT_MOUSE_CLICK,
    ROOT_INPUT_MOUSE_DOUBLE_CLICK,

    ROOT_INPUT_MOUSE_DRAG,

    ROOT_INPUT_MOUSE_WHEEL

} RootInputEventType;


/*
 * ============================================================
 * MOUSE BUTTONS
 * ============================================================
 */

typedef enum
{
    ROOT_MOUSE_NONE = 0,

    ROOT_MOUSE_LEFT,
    ROOT_MOUSE_RIGHT,
    ROOT_MOUSE_MIDDLE

} RootMouseButton;


/*
 * ============================================================
 * ROOT INPUT EVENT
 * ============================================================
 */

typedef struct
{
    /*
     * Tiempo desde el arranque
     * en milisegundos.
     */
    u64 timestamp_ms;


    /*
     * Tipo de evento.
     */
    RootInputEventType type;


    /*
     * ========================================================
     * KEYBOARD
     * ========================================================
     */

    RootKey key;

    RootCodepoint codepoint;

    bool shift;
    bool ctrl;
    bool alt;
    bool altgr;


    /*
     * ========================================================
     * MOUSE
     * ========================================================
     */

    i32 mouse_x;
    i32 mouse_y;

    i32 mouse_dx;
    i32 mouse_dy;

    /* Positive = wheel up, negative = wheel down. */
    i32 mouse_wheel;

    RootMouseButton button;

    u8 mouse_buttons;

} RootInputEvent;


/*
 * ============================================================
 * INITIALIZATION
 * ============================================================
 */

void rootinput_init(void);


/*
 * ============================================================
 * EVENT PROCESSING
 * ============================================================
 */

void rootinput_poll(void);


bool rootinput_next_event(
    RootInputEvent* event
);


RootInputEvent rootinput_wait_event(void);


/*
 * ============================================================
 * KEYBOARD STATE
 * ============================================================
 */

bool rootinput_key_down(
    RootKey key
);


/*
 * ============================================================
 * MOUSE STATE
 * ============================================================
 */

i32 rootinput_mouse_x(void);


i32 rootinput_mouse_y(void);


bool rootinput_mouse_available(void);


#endif