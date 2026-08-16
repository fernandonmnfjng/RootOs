#include "keymap_latam.h"


/*
 * ==========================================
 * CODIGOS DE LA FUENTE VGA ACTUAL (CP437)
 * ==========================================
 *
 * Esto es temporal.
 *
 * Cuando RootOS tenga framebuffer + fuente
 * propia cambiaremos todo a Unicode/UTF-8.
 */

#define CHAR_C_CEDILLA       0x87
#define CHAR_C_CEDILLA_UPPER 0x80

#define CHAR_U_DIAERESIS     0x81
#define CHAR_E_ACUTE         0x82
#define CHAR_A_CIRCUMFLEX    0x83
#define CHAR_A_DIAERESIS     0x84
#define CHAR_A_GRAVE         0x85

#define CHAR_E_CIRCUMFLEX    0x88
#define CHAR_E_DIAERESIS     0x89
#define CHAR_E_GRAVE         0x8A
#define CHAR_I_DIAERESIS     0x8B
#define CHAR_I_CIRCUMFLEX    0x8C
#define CHAR_I_GRAVE         0x8D

#define CHAR_A_DIAERESIS_UPPER 0x8E
#define CHAR_E_ACUTE_UPPER     0x90

#define CHAR_O_CIRCUMFLEX    0x93
#define CHAR_O_DIAERESIS     0x94
#define CHAR_O_GRAVE         0x95
#define CHAR_U_CIRCUMFLEX    0x96
#define CHAR_U_GRAVE         0x97

#define CHAR_O_DIAERESIS_UPPER 0x99
#define CHAR_U_DIAERESIS_UPPER 0x9A

#define CHAR_A_ACUTE         0xA0
#define CHAR_I_ACUTE         0xA1
#define CHAR_O_ACUTE         0xA2
#define CHAR_U_ACUTE         0xA3

#define CHAR_N_TILDE         0xA4
#define CHAR_N_TILDE_UPPER   0xA5

#define CHAR_QUESTION_DOWN   0xA8
#define CHAR_NOT             0xAA
#define CHAR_HALF            0xAB
#define CHAR_EXCLAM_DOWN     0xAD

#define CHAR_DEGREE          0xF8
#define CHAR_MIDDLE_DOT      0xFA


/*
 * ==========================================
 * DEAD KEYS
 * ==========================================
 */

typedef enum
{
    DEAD_NONE,

    DEAD_ACUTE,
    DEAD_DIAERESIS,
    DEAD_CIRCUMFLEX,
    DEAD_GRAVE,
    DEAD_CEDILLA

} DeadKey;


static DeadKey dead_key =
    DEAD_NONE;


/*
 * ==========================================
 * LETRAS
 * ==========================================
 */

static u8 letter_from_scancode(
    u8 scancode
)
{
    switch (scancode)
    {
        case 0x10: return 'q';
        case 0x11: return 'w';
        case 0x12: return 'e';
        case 0x13: return 'r';
        case 0x14: return 't';
        case 0x15: return 'y';
        case 0x16: return 'u';
        case 0x17: return 'i';
        case 0x18: return 'o';
        case 0x19: return 'p';

        case 0x1E: return 'a';
        case 0x1F: return 's';
        case 0x20: return 'd';
        case 0x21: return 'f';
        case 0x22: return 'g';
        case 0x23: return 'h';
        case 0x24: return 'j';
        case 0x25: return 'k';
        case 0x26: return 'l';

        case 0x2C: return 'z';
        case 0x2D: return 'x';
        case 0x2E: return 'c';
        case 0x2F: return 'v';
        case 0x30: return 'b';
        case 0x31: return 'n';
        case 0x32: return 'm';
    }

    return 0;
}


static int is_lowercase_letter(
    u8 c
)
{
    return (
        c >= 'a'
        &&
        c <= 'z'
    );
}


/*
 * ==========================================
 * COMPOSICION DE ACENTOS
 * ==========================================
 */

static u8 compose_dead_key(
    u8 character
)
{
    DeadKey old_dead =
        dead_key;

    dead_key =
        DEAD_NONE;


    /*
     * Dead key + espacio:
     *
     * Como VGA actual no tiene todos los
     * signos Unicode independientes,
     * usamos equivalentes ASCII.
     */

    if (character == ' ')
    {
        switch (old_dead)
        {
            case DEAD_ACUTE:
                return '\'';

            case DEAD_DIAERESIS:
                return '"';

            case DEAD_CIRCUMFLEX:
                return '^';

            case DEAD_GRAVE:
                return '`';

            case DEAD_CEDILLA:
                return ',';

            default:
                return ' ';
        }
    }


    /*
     * ACENTO AGUDO
     */

    if (old_dead == DEAD_ACUTE)
    {
        switch (character)
        {
            case 'a': return CHAR_A_ACUTE;
            case 'e': return CHAR_E_ACUTE;
            case 'i': return CHAR_I_ACUTE;
            case 'o': return CHAR_O_ACUTE;
            case 'u': return CHAR_U_ACUTE;

            case 'E': return CHAR_E_ACUTE_UPPER;
        }
    }


    /*
     * DIERESIS
     */

    if (old_dead == DEAD_DIAERESIS)
    {
        switch (character)
        {
            case 'a': return CHAR_A_DIAERESIS;
            case 'e': return CHAR_E_DIAERESIS;
            case 'i': return CHAR_I_DIAERESIS;
            case 'o': return CHAR_O_DIAERESIS;
            case 'u': return CHAR_U_DIAERESIS;

            case 'A': return CHAR_A_DIAERESIS_UPPER;
            case 'O': return CHAR_O_DIAERESIS_UPPER;
            case 'U': return CHAR_U_DIAERESIS_UPPER;
        }
    }


    /*
     * CIRCUNFLEJO
     */

    if (old_dead == DEAD_CIRCUMFLEX)
    {
        switch (character)
        {
            case 'a': return CHAR_A_CIRCUMFLEX;
            case 'e': return CHAR_E_CIRCUMFLEX;
            case 'i': return CHAR_I_CIRCUMFLEX;
            case 'o': return CHAR_O_CIRCUMFLEX;
            case 'u': return CHAR_U_CIRCUMFLEX;
        }
    }


    /*
     * ACENTO GRAVE
     */

    if (old_dead == DEAD_GRAVE)
    {
        switch (character)
        {
            case 'a': return CHAR_A_GRAVE;
            case 'e': return CHAR_E_GRAVE;
            case 'i': return CHAR_I_GRAVE;
            case 'o': return CHAR_O_GRAVE;
            case 'u': return CHAR_U_GRAVE;
        }
    }


    /*
     * CEDILLA
     */

    if (old_dead == DEAD_CEDILLA)
    {
        if (character == 'c')
        {
            return CHAR_C_CEDILLA;
        }

        if (character == 'C')
        {
            return CHAR_C_CEDILLA_UPPER;
        }
    }


    /*
     * No existia combinacion.
     */
    return character;
}


static u8 finish_character(
    u8 character
)
{
    if (
        dead_key != DEAD_NONE
    )
    {
        return compose_dead_key(
            character
        );
    }

    return character;
}


/*
 * ==========================================
 * KEYMAP LATINOAMERICANO
 * ==========================================
 */

u8 keymap_latam_translate(
    u8 scancode,
    int shift,
    int altgr,
    int caps_lock,
    int num_lock
)
{
    /*
     * Primero letras QWERTY.
     */

    u8 letter =
        letter_from_scancode(
            scancode
        );


    if (letter)
    {
        int uppercase =
            shift
            ^
            caps_lock;


        if (
            uppercase
            &&
            is_lowercase_letter(letter)
        )
        {
            letter =
                letter
                -
                'a'
                +
                'A';
        }


        return finish_character(
            letter
        );
    }


    /*
     * ======================================
     * FILA DE NUMEROS
     * ======================================
     */

    switch (scancode)
    {
        /*
         * 1 ! |
         */
        case 0x02:

            if (altgr)
                return finish_character('|');

            return finish_character(
                shift ? '!' : '1'
            );


        /*
         * 2 " @
         */
        case 0x03:

            if (altgr)
                return finish_character('@');

            return finish_character(
                shift ? '"' : '2'
            );


        /*
         * 3 # ·
         */
        case 0x04:

            if (altgr)
                return finish_character(
                    CHAR_MIDDLE_DOT
                );

            return finish_character(
                shift ? '#' : '3'
            );


        /*
         * 4 $ ~
         */
        case 0x05:

            if (altgr)
                return finish_character('~');

            return finish_character(
                shift ? '$' : '4'
            );


        /*
         * 5 % ½
         */
        case 0x06:

            if (altgr)
                return finish_character(
                    CHAR_HALF
                );

            return finish_character(
                shift ? '%' : '5'
            );


        /*
         * 6 & ¬
         */
        case 0x07:

            if (altgr)
                return finish_character(
                    CHAR_NOT
                );

            return finish_character(
                shift ? '&' : '6'
            );


        /*
         * 7 / {
         */
        case 0x08:

            if (altgr)
                return finish_character('{');

            return finish_character(
                shift ? '/' : '7'
            );


        /*
         * 8 ( [
         */
        case 0x09:

            if (altgr)
                return finish_character('[');

            return finish_character(
                shift ? '(' : '8'
            );


        /*
         * 9 ) ]
         */
        case 0x0A:

            if (altgr)
                return finish_character(']');

            return finish_character(
                shift ? ')' : '9'
            );


        /*
         * 0 = }
         */
        case 0x0B:

            if (altgr)
                return finish_character('}');

            return finish_character(
                shift ? '=' : '0'
            );


        /*
         * ' ? \
         */
        case 0x0C:

            if (altgr)
                return finish_character('\\');

            return finish_character(
                shift ? '?' : '\''
            );


        /*
         * ¿ ¡
         *
         * AltGr: cedilla como dead key.
         */
        case 0x0D:

            if (altgr)
            {
                dead_key =
                    DEAD_CEDILLA;

                return 0;
            }

            return finish_character(
                shift
                ?
                CHAR_EXCLAM_DOWN
                :
                CHAR_QUESTION_DOWN
            );
    }


    /*
     * ======================================
     * TECLAS DESPUES DE P
     * ======================================
     */

    /*
     * ´ / ¨
     */
    if (scancode == 0x1A)
    {
        if (shift || altgr)
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
    }


    /*
     * + * ~
     */
    if (scancode == 0x1B)
    {
        if (altgr)
            return finish_character('~');

        return finish_character(
            shift ? '*' : '+'
        );
    }


    /*
     * ======================================
     * Ñ
     * ======================================
     */

    if (scancode == 0x27)
    {
        if (altgr)
            return finish_character('~');

        return finish_character(
            shift
            ?
            CHAR_N_TILDE_UPPER
            :
            CHAR_N_TILDE
        );
    }


    /*
     * { [ ^
     */
    if (scancode == 0x28)
    {
        if (altgr)
            return finish_character('^');

        return finish_character(
            shift ? '[' : '{'
        );
    }


    /*
     * | ° ¬
     *
     * Tecla a la izquierda del 1
     * en teclado ISO/LatAm.
     */
    if (scancode == 0x29)
    {
        if (altgr)
            return finish_character(
                CHAR_NOT
            );

        if (shift)
            return finish_character(
                CHAR_DEGREE
            );

        return finish_character('|');
    }


    /*
     * } ] `
     */
    if (scancode == 0x2B)
    {
        if (altgr)
            return finish_character('`');

        return finish_character(
            shift ? ']' : '}'
        );
    }


    /*
     * ======================================
     * PARTE INFERIOR
     * ======================================
     */

    /*
     * , ;
     */
    if (scancode == 0x33)
    {
        return finish_character(
            shift ? ';' : ','
        );
    }


    /*
     * . :
     */
    if (scancode == 0x34)
    {
        return finish_character(
            shift ? ':' : '.'
        );
    }


    /*
     * - _
     */
    if (scancode == 0x35)
    {
        return finish_character(
            shift ? '_' : '-'
        );
    }


    /*
     * Tecla ISO extra:
     *
     * < > |
     */
    if (scancode == 0x56)
    {
        if (altgr)
            return finish_character('|');

        return finish_character(
            shift ? '>' : '<'
        );
    }


    /*
     * Espacio.
     */
    if (scancode == 0x39)
    {
        return finish_character(' ');
    }


    /*
     * ======================================
     * NUMPAD
     * ======================================
     */

    if (scancode == 0x37)
        return finish_character('*');

    if (scancode == 0x4A)
        return finish_character('-');

    if (scancode == 0x4E)
        return finish_character('+');


    if (num_lock)
    {
        switch (scancode)
        {
            case 0x47: return '7';
            case 0x48: return '8';
            case 0x49: return '9';

            case 0x4B: return '4';
            case 0x4C: return '5';
            case 0x4D: return '6';

            case 0x4F: return '1';
            case 0x50: return '2';
            case 0x51: return '3';

            case 0x52: return '0';
            case 0x53: return '.';
        }
    }


    return 0;
}


void keymap_latam_reset(void)
{
    dead_key =
        DEAD_NONE;
}
