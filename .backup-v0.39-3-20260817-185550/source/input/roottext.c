#include "roottext.h"


/*
 * ============================================================
 * VALID KEY DOWN
 * ============================================================
 */

static bool roottext_is_key_down(
    const RootInputEvent* event
)
{
    if (
        event == NULL
    )
    {
        return false;
    }


    return
        event->type
        ==
        ROOT_INPUT_KEY_DOWN;
}


/*
 * ============================================================
 * NORMAL CTRL SHORTCUT
 * ============================================================
 *
 * AltGr NO cuenta como Ctrl+Alt.
 *
 * Esto es importante para teclados LATAM:
 *
 * AltGr + Q
 * AltGr + 2
 * etc.
 *
 * deben seguir pudiendo producir caracteres.
 */

static bool roottext_plain_ctrl(
    const RootInputEvent* event
)
{
    if (
        !roottext_is_key_down(
            event
        )
    )
    {
        return false;
    }


    if (
        event->altgr
    )
    {
        return false;
    }


    if (
        !event->ctrl
    )
    {
        return false;
    }


    if (
        event->alt
    )
    {
        return false;
    }


    return true;
}


/*
 * ============================================================
 * SHOULD INSERT CHARACTER
 * ============================================================
 */

bool roottext_should_insert(
    const RootInputEvent* event
)
{
    if (
        !roottext_is_key_down(
            event
        )
    )
    {
        return false;
    }


    if (
        event->codepoint
        ==
        0
    )
    {
        return false;
    }


    /*
     * AltGr es modificador de escritura.
     *
     * Por ejemplo:
     *
     * @
     * |
     * ~
     * {
     * }
     *
     * dependiendo del layout.
     */

    if (
        event->altgr
    )
    {
        return true;
    }


    /*
     * Ctrl + cualquier tecla:
     *
     * nunca insertar texto.
     */

    if (
        event->ctrl
    )
    {
        return false;
    }


    /*
     * Alt + cualquier tecla:
     *
     * tampoco insertar texto.
     */

    if (
        event->alt
    )
    {
        return false;
    }


    /*
     * Shift sí puede producir texto.
     *
     * a -> A
     * 1 -> !
     * etc.
     */

    return true;
}


/*
 * ============================================================
 * EDITOR SHORTCUTS
 * ============================================================
 */

RootTextAction roottext_editor_action(
    const RootInputEvent* event
)
{
    if (
        !roottext_plain_ctrl(
            event
        )
    )
    {
        return
            ROOT_TEXT_ACTION_NONE;
    }


    /*
     * Ctrl+A
     */

    if (
        event->key
        ==
        ROOT_KEY_A
    )
    {
        return
            ROOT_TEXT_ACTION_SELECT_ALL;
    }


    /*
     * Ctrl+C
     */

    if (
        event->key
        ==
        ROOT_KEY_C
    )
    {
        return
            ROOT_TEXT_ACTION_COPY;
    }


    /*
     * Ctrl+X
     */

    if (
        event->key
        ==
        ROOT_KEY_X
    )
    {
        return
            ROOT_TEXT_ACTION_CUT;
    }


    /*
     * Ctrl+V
     */

    if (
        event->key
        ==
        ROOT_KEY_V
    )
    {
        return
            ROOT_TEXT_ACTION_PASTE;
    }


    /*
     * Ctrl+Z
     */

    if (
        event->key
        ==
        ROOT_KEY_Z
    )
    {
        return
            ROOT_TEXT_ACTION_UNDO;
    }


    /*
     * Ctrl+Y
     */

    if (
        event->key
        ==
        ROOT_KEY_Y
    )
    {
        return
            ROOT_TEXT_ACTION_REDO;
    }


    /*
     * Ctrl+S
     */

    if (
        event->key
        ==
        ROOT_KEY_S
    )
    {
        return
            ROOT_TEXT_ACTION_SAVE;
    }


    return
        ROOT_TEXT_ACTION_NONE;
}


/*
 * ============================================================
 * TERMINAL SHORTCUTS
 * ============================================================
 */

RootTextAction roottext_terminal_action(
    const RootInputEvent* event
)
{
    if (
        !roottext_is_key_down(
            event
        )
    )
    {
        return
            ROOT_TEXT_ACTION_NONE;
    }


    /*
     * AltGr jamás debe convertirse
     * en un shortcut del terminal.
     */

    if (
        event->altgr
    )
    {
        return
            ROOT_TEXT_ACTION_NONE;
    }


    /*
     * ========================================================
     * CTRL + SHIFT
     * ========================================================
     *
     * Convención habitual de terminal:
     *
     * Ctrl+Shift+C = copy
     * Ctrl+Shift+V = paste
     */

    if (
        event->ctrl
        &&
        event->shift
        &&
        !event->alt
    )
    {
        if (
            event->key
            ==
            ROOT_KEY_C
        )
        {
            return
                ROOT_TEXT_ACTION_TERMINAL_COPY;
        }


        if (
            event->key
            ==
            ROOT_KEY_V
        )
        {
            return
                ROOT_TEXT_ACTION_TERMINAL_PASTE;
        }


        return
            ROOT_TEXT_ACTION_NONE;
    }


    /*
     * Los demás requieren Ctrl solo.
     */

    if (
        !event->ctrl
        ||
        event->alt
    )
    {
        return
            ROOT_TEXT_ACTION_NONE;
    }


    /*
     * Ctrl+C
     *
     * Interrumpir/cancelar.
     */

    if (
        event->key
        ==
        ROOT_KEY_C
    )
    {
        return
            ROOT_TEXT_ACTION_INTERRUPT;
    }


    /*
     * Ctrl+L
     *
     * Limpiar terminal.
     */

    if (
        event->key
        ==
        ROOT_KEY_L
    )
    {
        return
            ROOT_TEXT_ACTION_CLEAR_SCREEN;
    }


    /*
     * Ctrl+A
     *
     * Inicio de línea.
     */

    if (
        event->key
        ==
        ROOT_KEY_A
    )
    {
        return
            ROOT_TEXT_ACTION_LINE_START;
    }


    /*
     * Ctrl+E
     *
     * Final de línea.
     */

    if (
        event->key
        ==
        ROOT_KEY_E
    )
    {
        return
            ROOT_TEXT_ACTION_LINE_END;
    }


    /*
     * Ctrl+Z
     *
     * Reservado para suspender procesos.
     *
     * Todavía no tenemos procesos suspendibles,
     * pero dejamos el significado correcto desde ahora.
     */

    if (
        event->key
        ==
        ROOT_KEY_Z
    )
    {
        return
            ROOT_TEXT_ACTION_SUSPEND;
    }


    return
        ROOT_TEXT_ACTION_NONE;
}