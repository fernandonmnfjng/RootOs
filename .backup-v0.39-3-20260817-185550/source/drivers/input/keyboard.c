#include "keyboard.h"
#include "keymap_latam.h"


static bool left_shift = false;
static bool right_shift = false;

static bool left_ctrl = false;
static bool right_ctrl = false;

static bool left_alt = false;
static bool right_alt = false;

static bool caps_lock = false;
static bool num_lock = false;

static bool extended = false;


/*
 * ============================================================
 * SCANCODE -> ROOT KEY
 * ============================================================
 */

static RootKey normal_key(
    u8 code
)
{
    switch (code)
    {
        case 0x01: return ROOT_KEY_ESCAPE;

        case 0x02: return ROOT_KEY_1;
        case 0x03: return ROOT_KEY_2;
        case 0x04: return ROOT_KEY_3;
        case 0x05: return ROOT_KEY_4;
        case 0x06: return ROOT_KEY_5;
        case 0x07: return ROOT_KEY_6;
        case 0x08: return ROOT_KEY_7;
        case 0x09: return ROOT_KEY_8;
        case 0x0A: return ROOT_KEY_9;
        case 0x0B: return ROOT_KEY_0;

        case 0x0C: return ROOT_KEY_MINUS;
        case 0x0D: return ROOT_KEY_EQUAL;

        case 0x0E: return ROOT_KEY_BACKSPACE;
        case 0x0F: return ROOT_KEY_TAB;

        case 0x10: return ROOT_KEY_Q;
        case 0x11: return ROOT_KEY_W;
        case 0x12: return ROOT_KEY_E;
        case 0x13: return ROOT_KEY_R;
        case 0x14: return ROOT_KEY_T;
        case 0x15: return ROOT_KEY_Y;
        case 0x16: return ROOT_KEY_U;
        case 0x17: return ROOT_KEY_I;
        case 0x18: return ROOT_KEY_O;
        case 0x19: return ROOT_KEY_P;

        case 0x1A: return ROOT_KEY_LEFT_BRACKET;
        case 0x1B: return ROOT_KEY_RIGHT_BRACKET;

        case 0x1C: return ROOT_KEY_ENTER;

        case 0x1D: return ROOT_KEY_LEFT_CTRL;

        case 0x1E: return ROOT_KEY_A;
        case 0x1F: return ROOT_KEY_S;
        case 0x20: return ROOT_KEY_D;
        case 0x21: return ROOT_KEY_F;
        case 0x22: return ROOT_KEY_G;
        case 0x23: return ROOT_KEY_H;
        case 0x24: return ROOT_KEY_J;
        case 0x25: return ROOT_KEY_K;
        case 0x26: return ROOT_KEY_L;

        case 0x27: return ROOT_KEY_SEMICOLON;
        case 0x28: return ROOT_KEY_APOSTROPHE;
        case 0x29: return ROOT_KEY_GRAVE;

        case 0x2A: return ROOT_KEY_LEFT_SHIFT;
        case 0x2B: return ROOT_KEY_BACKSLASH;

        case 0x2C: return ROOT_KEY_Z;
        case 0x2D: return ROOT_KEY_X;
        case 0x2E: return ROOT_KEY_C;
        case 0x2F: return ROOT_KEY_V;
        case 0x30: return ROOT_KEY_B;
        case 0x31: return ROOT_KEY_N;
        case 0x32: return ROOT_KEY_M;

        case 0x33: return ROOT_KEY_COMMA;
        case 0x34: return ROOT_KEY_DOT;
        case 0x35: return ROOT_KEY_SLASH;

        case 0x36: return ROOT_KEY_RIGHT_SHIFT;

        case 0x37: return ROOT_KEY_KP_MULTIPLY;

        case 0x38: return ROOT_KEY_LEFT_ALT;

        case 0x39: return ROOT_KEY_SPACE;

        case 0x3A: return ROOT_KEY_CAPS_LOCK;

        case 0x3B: return ROOT_KEY_F1;
        case 0x3C: return ROOT_KEY_F2;
        case 0x3D: return ROOT_KEY_F3;
        case 0x3E: return ROOT_KEY_F4;
        case 0x3F: return ROOT_KEY_F5;
        case 0x40: return ROOT_KEY_F6;
        case 0x41: return ROOT_KEY_F7;
        case 0x42: return ROOT_KEY_F8;
        case 0x43: return ROOT_KEY_F9;
        case 0x44: return ROOT_KEY_F10;

        case 0x45: return ROOT_KEY_NUM_LOCK;
        case 0x46: return ROOT_KEY_SCROLL_LOCK;

        case 0x47: return ROOT_KEY_KP_7;
        case 0x48: return ROOT_KEY_KP_8;
        case 0x49: return ROOT_KEY_KP_9;

        case 0x4A: return ROOT_KEY_KP_MINUS;

        case 0x4B: return ROOT_KEY_KP_4;
        case 0x4C: return ROOT_KEY_KP_5;
        case 0x4D: return ROOT_KEY_KP_6;

        case 0x4E: return ROOT_KEY_KP_PLUS;

        case 0x4F: return ROOT_KEY_KP_1;
        case 0x50: return ROOT_KEY_KP_2;
        case 0x51: return ROOT_KEY_KP_3;

        case 0x52: return ROOT_KEY_KP_0;
        case 0x53: return ROOT_KEY_KP_DOT;

        case 0x56: return ROOT_KEY_ISO_LTGT;

        case 0x57: return ROOT_KEY_F11;
        case 0x58: return ROOT_KEY_F12;
    }


    return ROOT_KEY_UNKNOWN;
}


static RootKey extended_key(
    u8 code
)
{
    switch (code)
    {
        case 0x1C:
            return ROOT_KEY_KP_ENTER;

        case 0x1D:
            return ROOT_KEY_RIGHT_CTRL;

        case 0x35:
            return ROOT_KEY_KP_DIVIDE;

        case 0x38:
            return ROOT_KEY_RIGHT_ALT;

        case 0x47:
            return ROOT_KEY_HOME;

        case 0x48:
            return ROOT_KEY_UP;

        case 0x49:
            return ROOT_KEY_PAGE_UP;

        case 0x4B:
            return ROOT_KEY_LEFT;

        case 0x4D:
            return ROOT_KEY_RIGHT;

        case 0x4F:
            return ROOT_KEY_END;

        case 0x50:
            return ROOT_KEY_DOWN;

        case 0x51:
            return ROOT_KEY_PAGE_DOWN;

        case 0x52:
            return ROOT_KEY_INSERT;

        case 0x53:
            return ROOT_KEY_DELETE;
    }


    return ROOT_KEY_UNKNOWN;
}


/*
 * ============================================================
 * MODIFIERS
 * ============================================================
 */

static void update_modifier(
    RootKey key,
    bool pressed
)
{
    switch (key)
    {
        case ROOT_KEY_LEFT_SHIFT:
            left_shift = pressed;
            break;

        case ROOT_KEY_RIGHT_SHIFT:
            right_shift = pressed;
            break;

        case ROOT_KEY_LEFT_CTRL:
            left_ctrl = pressed;
            break;

        case ROOT_KEY_RIGHT_CTRL:
            right_ctrl = pressed;
            break;

        case ROOT_KEY_LEFT_ALT:
            left_alt = pressed;
            break;

        case ROOT_KEY_RIGHT_ALT:
            right_alt = pressed;
            break;

        default:
            break;
    }
}


void keyboard_reset(void)
{
    left_shift = false;
    right_shift = false;

    left_ctrl = false;
    right_ctrl = false;

    left_alt = false;
    right_alt = false;

    caps_lock = false;
    num_lock = false;

    extended = false;

    keymap_latam_reset();
}


bool keyboard_feed_byte(
    u8 scancode,
    KeyboardEvent* event
)
{
    if (
        event == NULL
    )
    {
        return false;
    }


    /*
     * E0 = siguiente scancode extendido.
     */
    if (
        scancode == 0xE0
    )
    {
        extended = true;

        return false;
    }


    bool released =
        (
            scancode
            &
            0x80
        )
        !=
        0;


    u8 code =
        scancode
        &
        0x7F;


    RootKey key;


    if (extended)
    {
        key =
            extended_key(
                code
            );

        extended =
            false;
    }

    else
    {
        key =
            normal_key(
                code
            );
    }


    if (
        key == ROOT_KEY_UNKNOWN
    )
    {
        return false;
    }


    bool pressed =
        !released;


    /*
     * Actualizar modificadores primero.
     */
    update_modifier(
        key,
        pressed
    );


    /*
     * Locks solo cambian al presionar.
     */
    if (pressed)
    {
        if (
            key == ROOT_KEY_CAPS_LOCK
        )
        {
            caps_lock =
                !caps_lock;
        }


        if (
            key == ROOT_KEY_NUM_LOCK
        )
        {
            num_lock =
                !num_lock;
        }
    }


    bool shift =
        left_shift
        ||
        right_shift;


    bool ctrl =
        left_ctrl
        ||
        right_ctrl;


    /*
     * Right Alt = AltGr.
     */
    bool altgr =
        right_alt;


    bool alt =
        left_alt;


    RootCodepoint codepoint =
        0;


    /*
     * Solo KEY DOWN genera texto.
     */
    if (pressed)
    {
        codepoint =
            keymap_latam_translate(
                key,
                shift,
                altgr,
                caps_lock,
                num_lock
            );
    }


    event->key =
        key;

    event->codepoint =
        codepoint;

    event->pressed =
        pressed;

    event->shift =
        shift;

    event->ctrl =
        ctrl;

    event->alt =
        alt;

    event->altgr =
        altgr;

    event->caps_lock =
        caps_lock;

    event->num_lock =
        num_lock;


    return true;
}