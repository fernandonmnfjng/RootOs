#include "keymap_latam.h"


typedef enum
{
    DEAD_NONE = 0,

    DEAD_ACUTE,
    DEAD_DIAERESIS,
    DEAD_CIRCUMFLEX,
    DEAD_GRAVE

} DeadKey;


static DeadKey dead_key =
    DEAD_NONE;


/*
 * ============================================================
 * LETRAS
 * ============================================================
 */

static RootCodepoint key_letter(
    RootKey key
)
{
    switch (key)
    {
        case ROOT_KEY_A: return 'a';
        case ROOT_KEY_B: return 'b';
        case ROOT_KEY_C: return 'c';
        case ROOT_KEY_D: return 'd';
        case ROOT_KEY_E: return 'e';
        case ROOT_KEY_F: return 'f';
        case ROOT_KEY_G: return 'g';
        case ROOT_KEY_H: return 'h';
        case ROOT_KEY_I: return 'i';
        case ROOT_KEY_J: return 'j';
        case ROOT_KEY_K: return 'k';
        case ROOT_KEY_L: return 'l';
        case ROOT_KEY_M: return 'm';
        case ROOT_KEY_N: return 'n';
        case ROOT_KEY_O: return 'o';
        case ROOT_KEY_P: return 'p';
        case ROOT_KEY_Q: return 'q';
        case ROOT_KEY_R: return 'r';
        case ROOT_KEY_S: return 's';
        case ROOT_KEY_T: return 't';
        case ROOT_KEY_U: return 'u';
        case ROOT_KEY_V: return 'v';
        case ROOT_KEY_W: return 'w';
        case ROOT_KEY_X: return 'x';
        case ROOT_KEY_Y: return 'y';
        case ROOT_KEY_Z: return 'z';

        default:
            return 0;
    }
}


static RootCodepoint upper(
    RootCodepoint c
)
{
    if (
        c >= 'a'
        &&
        c <= 'z'
    )
    {
        return
            c
            -
            'a'
            +
            'A';
    }


    return c;
}


/*
 * ============================================================
 * DEAD KEYS
 * ============================================================
 */

static RootCodepoint compose(
    RootCodepoint character
)
{
    DeadKey previous =
        dead_key;


    dead_key =
        DEAD_NONE;


    if (
        character == ' '
    )
    {
        switch (previous)
        {
            case DEAD_ACUTE:
                return 0x00B4; /* ´ */

            case DEAD_DIAERESIS:
                return 0x00A8; /* ¨ */

            case DEAD_CIRCUMFLEX:
                return '^';

            case DEAD_GRAVE:
                return '`';

            default:
                return ' ';
        }
    }


    if (
        previous == DEAD_ACUTE
    )
    {
        switch (character)
        {
            case 'a': return 0x00E1;
            case 'e': return 0x00E9;
            case 'i': return 0x00ED;
            case 'o': return 0x00F3;
            case 'u': return 0x00FA;

            case 'A': return 0x00C1;
            case 'E': return 0x00C9;
            case 'I': return 0x00CD;
            case 'O': return 0x00D3;
            case 'U': return 0x00DA;
        }
    }


    if (
        previous == DEAD_DIAERESIS
    )
    {
        switch (character)
        {
            case 'a': return 0x00E4;
            case 'e': return 0x00EB;
            case 'i': return 0x00EF;
            case 'o': return 0x00F6;
            case 'u': return 0x00FC;

            case 'A': return 0x00C4;
            case 'E': return 0x00CB;
            case 'I': return 0x00CF;
            case 'O': return 0x00D6;
            case 'U': return 0x00DC;
        }
    }


    if (
        previous == DEAD_CIRCUMFLEX
    )
    {
        switch (character)
        {
            case 'a': return 0x00E2;
            case 'e': return 0x00EA;
            case 'i': return 0x00EE;
            case 'o': return 0x00F4;
            case 'u': return 0x00FB;

            case 'A': return 0x00C2;
            case 'E': return 0x00CA;
            case 'I': return 0x00CE;
            case 'O': return 0x00D4;
            case 'U': return 0x00DB;
        }
    }


    if (
        previous == DEAD_GRAVE
    )
    {
        switch (character)
        {
            case 'a': return 0x00E0;
            case 'e': return 0x00E8;
            case 'i': return 0x00EC;
            case 'o': return 0x00F2;
            case 'u': return 0x00F9;

            case 'A': return 0x00C0;
            case 'E': return 0x00C8;
            case 'I': return 0x00CC;
            case 'O': return 0x00D2;
            case 'U': return 0x00D9;
        }
    }


    return character;
}


static RootCodepoint finish(
    RootCodepoint character
)
{
    if (
        dead_key != DEAD_NONE
    )
    {
        return compose(
            character
        );
    }


    return character;
}


/*
 * ============================================================
 * KEYMAP
 * ============================================================
 */

RootCodepoint keymap_latam_translate(
    RootKey key,
    bool shift,
    bool altgr,
    bool caps_lock,
    bool num_lock
)
{
    /*
     * Euro.
     */
    if (
        key == ROOT_KEY_E
        &&
        altgr
    )
    {
        return 0x20AC;
    }


    /*
     * Letras.
     */
    RootCodepoint letter =
        key_letter(
            key
        );


    if (letter)
    {
        if (
            shift
            ^
            caps_lock
        )
        {
            letter =
                upper(
                    letter
                );
        }


        return finish(
            letter
        );
    }


    switch (key)
    {
        /*
         * 1 ! |
         */
        case ROOT_KEY_1:

            if (altgr)
                return finish('|');

            return finish(
                shift ? '!' : '1'
            );


        /*
         * 2 " @
         */
        case ROOT_KEY_2:

            if (altgr)
                return finish('@');

            return finish(
                shift ? '"' : '2'
            );


        /*
         * 3 # ·
         */
        case ROOT_KEY_3:

            if (altgr)
                return finish(0x00B7);

            return finish(
                shift ? '#' : '3'
            );


        /*
         * 4 $ ~
         */
        case ROOT_KEY_4:

            if (altgr)
                return finish('~');

            return finish(
                shift ? '$' : '4'
            );


        case ROOT_KEY_5:

            return finish(
                shift ? '%' : '5'
            );


        /*
         * 6 & ¬
         */
        case ROOT_KEY_6:

            if (altgr)
                return finish(0x00AC);

            return finish(
                shift ? '&' : '6'
            );


        /*
         * 7 / {
         */
        case ROOT_KEY_7:

            if (altgr)
                return finish('{');

            return finish(
                shift ? '/' : '7'
            );


        /*
         * 8 ( [
         */
        case ROOT_KEY_8:

            if (altgr)
                return finish('[');

            return finish(
                shift ? '(' : '8'
            );


        /*
         * 9 ) ]
         */
        case ROOT_KEY_9:

            if (altgr)
                return finish(']');

            return finish(
                shift ? ')' : '9'
            );


        /*
         * 0 = }
         */
        case ROOT_KEY_0:

            if (altgr)
                return finish('}');

            return finish(
                shift ? '=' : '0'
            );


        /*
         * Tecla física -.
         *
         * ' ? \
         */
        case ROOT_KEY_MINUS:

            if (altgr)
                return finish('\\');

            return finish(
                shift ? '?' : '\''
            );


        /*
         * ¿ ¡
         */
        case ROOT_KEY_EQUAL:

            return finish(
                shift
                ?
                0x00A1
                :
                0x00BF
            );


        /*
         * Dead key:
         *
         * ´ / ¨
         */
        case ROOT_KEY_LEFT_BRACKET:

            if (altgr)
            {
                dead_key =
                    DEAD_GRAVE;
            }

            else if (shift)
            {
                dead_key =
                    DEAD_DIAERESIS;
            }

            else
            {
                dead_key =
                    DEAD_ACUTE;
            }

            return 0;


        /*
         * + * ~
         */
        case ROOT_KEY_RIGHT_BRACKET:

            if (altgr)
                return finish('~');

            return finish(
                shift ? '*' : '+'
            );


        /*
         * Ñ.
         *
         * Esta es la posición física
         * del ; en un teclado US.
         */
        case ROOT_KEY_SEMICOLON:

            return finish(
                shift
                ?
                0x00D1
                :
                0x00F1
            );


        /*
         * { [ ^
         */
        case ROOT_KEY_APOSTROPHE:

            if (altgr)
            {
                dead_key =
                    DEAD_CIRCUMFLEX;

                return 0;
            }

            return finish(
                shift ? '[' : '{'
            );


        /*
         * | ° ¬
         */
        case ROOT_KEY_GRAVE:

            if (altgr)
                return finish(0x00AC);

            return finish(
                shift
                ?
                0x00B0
                :
                '|'
            );


        /*
         * } ] `
         */
        case ROOT_KEY_BACKSLASH:

            if (altgr)
                return finish('`');

            return finish(
                shift ? ']' : '}'
            );


        case ROOT_KEY_ISO_LTGT:

            if (altgr)
                return finish('|');

            return finish(
                shift ? '>' : '<'
            );


        case ROOT_KEY_COMMA:

            return finish(
                shift ? ';' : ','
            );


        case ROOT_KEY_DOT:

            return finish(
                shift ? ':' : '.'
            );


        case ROOT_KEY_SLASH:

            return finish(
                shift ? '_' : '-'
            );


        case ROOT_KEY_SPACE:

            return finish(' ');


        case ROOT_KEY_KP_MULTIPLY:
            return '*';

        case ROOT_KEY_KP_DIVIDE:
            return '/';

        case ROOT_KEY_KP_PLUS:
            return '+';

        case ROOT_KEY_KP_MINUS:
            return '-';


        default:
            break;
    }


    if (num_lock)
    {
        switch (key)
        {
            case ROOT_KEY_KP_0: return '0';
            case ROOT_KEY_KP_1: return '1';
            case ROOT_KEY_KP_2: return '2';
            case ROOT_KEY_KP_3: return '3';
            case ROOT_KEY_KP_4: return '4';
            case ROOT_KEY_KP_5: return '5';
            case ROOT_KEY_KP_6: return '6';
            case ROOT_KEY_KP_7: return '7';
            case ROOT_KEY_KP_8: return '8';
            case ROOT_KEY_KP_9: return '9';
            case ROOT_KEY_KP_DOT: return '.';

            default:
                break;
        }
    }


    return 0;
}


void keymap_latam_reset(void)
{
    dead_key =
        DEAD_NONE;
}