#ifndef ROOTOS_KEYBOARD_H
#define ROOTOS_KEYBOARD_H


typedef enum
{
    KEY_NONE,

    KEY_CHARACTER,

    KEY_ENTER,
    KEY_BACKSPACE,
    KEY_TAB,
    KEY_ESCAPE,

    KEY_LEFT,
    KEY_RIGHT,
    KEY_UP,
    KEY_DOWN,

    KEY_HOME,
    KEY_END,

    KEY_INSERT,
    KEY_DELETE,

    KEY_PAGE_UP,
    KEY_PAGE_DOWN,

    KEY_F1,
    KEY_F2,
    KEY_F3,
    KEY_F4,
    KEY_F5,
    KEY_F6,
    KEY_F7,
    KEY_F8,
    KEY_F9,
    KEY_F10,
    KEY_F11,
    KEY_F12

} KeyType;


typedef struct
{
    KeyType type;

    /*
     * Caracter de 8 bits que por ahora
     * utiliza nuestra terminal VGA.
     */
    unsigned char character;

    int shift;
    int ctrl;

    /*
     * Alt izquierdo.
     */
    int alt;

    /*
     * Alt derecho / AltGr.
     */
    int altgr;

    int caps_lock;
    int num_lock;

} KeyEvent;


KeyEvent keyboard_read_event(void);


#endif
