#include "keyboard.h"
#include "keymap_latam.h"
#include "io.h"


static int shift_down = 0;
static int ctrl_down = 0;
static int alt_down = 0;
static int altgr_down = 0;

static int caps_lock = 0;
static int num_lock = 0;


/*
 * ==========================================
 * CREAR EVENTO
 * ==========================================
 */

static KeyEvent make_event(
    KeyType type,
    unsigned char character
)
{
    KeyEvent event;

    event.type =
    type;

    event.character =
    character;

    event.shift =
    shift_down;

    event.ctrl =
    ctrl_down;

    event.alt =
    alt_down;

    event.altgr =
    altgr_down;

    event.caps_lock =
    caps_lock;

    event.num_lock =
    num_lock;

    return event;
}


/*
 * ==========================================
 * TECLADO PS/2
 * ==========================================
 */

KeyEvent keyboard_read_event(void)
{
    int extended = 0;


    while (1)
    {
        /*
         * Esperar a que el controlador
         * tenga datos disponibles.
         */
        while (
            (inb(0x64) & 1)
            ==
            0
        )
        {
        }


        u8 scancode =
        inb(0x60);


        /*
         * Prefijo E0.
         *
         * Flechas, AltGr, Ctrl derecho,
         * Delete, Home, etc.
         */
        if (
            scancode == 0xE0
        )
        {
            extended = 1;

            continue;
        }


        int released =
        (scancode & 0x80)
        !=
        0;


        u8 code =
        scancode & 0x7F;


        /*
         * ==================================
         * TECLA LIBERADA
         * ==================================
         */

        if (released)
        {
            if (extended)
            {
                /*
                 * Ctrl derecho.
                 */
                if (code == 0x1D)
                {
                    ctrl_down = 0;
                }

                /*
                 * Alt derecho = AltGr.
                 */
                else if (
                    code == 0x38
                )
                {
                    altgr_down = 0;
                }


                extended = 0;

                continue;
            }


            /*
             * Shift izquierdo/derecho.
             */
            if (
                code == 0x2A
                ||
                code == 0x36
            )
            {
                shift_down = 0;
            }


            /*
             * Ctrl izquierdo.
             */
            else if (
                code == 0x1D
            )
            {
                ctrl_down = 0;
            }


            /*
             * Alt izquierdo.
             */
            else if (
                code == 0x38
            )
            {
                alt_down = 0;
            }


            continue;
        }


        /*
         * ==================================
         * TECLAS EXTENDIDAS
         * ==================================
         */

        if (extended)
        {
            extended = 0;


            /*
             * Ctrl derecho.
             */
            if (code == 0x1D)
            {
                ctrl_down = 1;

                continue;
            }


            /*
             * Alt derecho / AltGr.
             */
            if (code == 0x38)
            {
                altgr_down = 1;

                continue;
            }


            /*
             * Enter del numpad.
             */
            if (code == 0x1C)
            {
                return make_event(
                    KEY_ENTER,
                    '\n'
                );
            }


            /*
             * / del numpad.
             */
            if (code == 0x35)
            {
                return make_event(
                    KEY_CHARACTER,
                    '/'
                );
            }


            switch (code)
            {
                case 0x4B:

                    return make_event(
                        KEY_LEFT,
                        0
                    );


                case 0x4D:

                    return make_event(
                        KEY_RIGHT,
                        0
                    );


                case 0x48:

                    return make_event(
                        KEY_UP,
                        0
                    );


                case 0x50:

                    return make_event(
                        KEY_DOWN,
                        0
                    );


                case 0x47:

                    return make_event(
                        KEY_HOME,
                        0
                    );


                case 0x4F:

                    return make_event(
                        KEY_END,
                        0
                    );


                case 0x52:

                    return make_event(
                        KEY_INSERT,
                        0
                    );


                case 0x53:

                    return make_event(
                        KEY_DELETE,
                        0
                    );


                case 0x49:

                    return make_event(
                        KEY_PAGE_UP,
                        0
                    );


                case 0x51:

                    return make_event(
                        KEY_PAGE_DOWN,
                        0
                    );
            }


            continue;
        }


        /*
         * ==================================
         * MODIFICADORES
         * ==================================
         */

        if (
            code == 0x2A
            ||
            code == 0x36
        )
        {
            shift_down = 1;

            continue;
        }


        if (code == 0x1D)
        {
            ctrl_down = 1;

            continue;
        }


        if (code == 0x38)
        {
            alt_down = 1;

            continue;
        }


        /*
         * Caps Lock.
         */
        if (code == 0x3A)
        {
            caps_lock =
            !caps_lock;

            continue;
        }


        /*
         * Num Lock.
         */
        if (code == 0x45)
        {
            num_lock =
            !num_lock;

            continue;
        }


        /*
         * ==================================
         * TECLAS DE CONTROL
         * ==================================
         */

        if (code == 0x01)
        {
            return make_event(
                KEY_ESCAPE,
                0
            );
        }


        if (code == 0x0E)
        {
            return make_event(
                KEY_BACKSPACE,
                '\b'
            );
        }


        if (code == 0x0F)
        {
            return make_event(
                KEY_TAB,
                '\t'
            );
        }


        if (code == 0x1C)
        {
            return make_event(
                KEY_ENTER,
                '\n'
            );
        }


        /*
         * ==================================
         * F1 - F12
         * ==================================
         */

        if (
            code >= 0x3B
            &&
            code <= 0x44
        )
        {
            return make_event(
                (KeyType)(
                    KEY_F1
                    +
                    (code - 0x3B)
                ),
                0
            );
        }


        if (code == 0x57)
        {
            return make_event(
                KEY_F11,
                0
            );
        }


        if (code == 0x58)
        {
            return make_event(
                KEY_F12,
                0
            );
        }


        /*
         * ==================================
         * CARACTER NORMAL
         * ==================================
         */

        u8 character =
        keymap_latam_translate(
            code,
            shift_down,
            altgr_down,
            caps_lock,
            num_lock
        );


        /*
         * Algunos scancodes son dead keys.
         *
         * Es correcto que devuelvan 0
         * hasta recibir la siguiente tecla.
         */
        if (character == 0)
        {
            continue;
        }


        return make_event(
            KEY_CHARACTER,
            character
        );
    }
}
