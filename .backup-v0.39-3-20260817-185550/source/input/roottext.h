#ifndef ROOTOS_ROOTTEXT_H
#define ROOTOS_ROOTTEXT_H

#include "types.h"
#include "rootinput.h"


/*
 * ============================================================
 * STANDARD TEXT ACTIONS
 * ============================================================
 */

typedef enum
{
    ROOT_TEXT_ACTION_NONE = 0,


    /*
     * Common editor actions.
     */

    ROOT_TEXT_ACTION_SELECT_ALL,

    ROOT_TEXT_ACTION_COPY,

    ROOT_TEXT_ACTION_CUT,

    ROOT_TEXT_ACTION_PASTE,

    ROOT_TEXT_ACTION_UNDO,

    ROOT_TEXT_ACTION_REDO,

    ROOT_TEXT_ACTION_SAVE,


    /*
     * Terminal actions.
     */

    ROOT_TEXT_ACTION_INTERRUPT,

    ROOT_TEXT_ACTION_CLEAR_SCREEN,

    ROOT_TEXT_ACTION_LINE_START,

    ROOT_TEXT_ACTION_LINE_END,

    ROOT_TEXT_ACTION_SUSPEND,


    /*
     * Terminal clipboard uses Ctrl+Shift.
     */

    ROOT_TEXT_ACTION_TERMINAL_COPY,

    ROOT_TEXT_ACTION_TERMINAL_PASTE

} RootTextAction;


/*
 * ============================================================
 * TEXT INPUT
 * ============================================================
 *
 * true:
 * el evento representa un carácter que debe escribirse.
 *
 * false:
 * Ctrl/Alt/etc. están actuando como comandos.
 */

bool roottext_should_insert(
    const RootInputEvent* event
);


/*
 * ============================================================
 * EDITOR SHORTCUT
 * ============================================================
 */

RootTextAction roottext_editor_action(
    const RootInputEvent* event
);


/*
 * ============================================================
 * TERMINAL SHORTCUT
 * ============================================================
 */

RootTextAction roottext_terminal_action(
    const RootInputEvent* event
);


#endif