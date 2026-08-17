#ifndef ROOTOS_ROOTTEXT_H
#define ROOTOS_ROOTTEXT_H

#include "types.h"
#include "rootinput.h"

typedef enum
{
    ROOT_TEXT_ACTION_NONE = 0,

    ROOT_TEXT_ACTION_SELECT_ALL,
    ROOT_TEXT_ACTION_COPY,
    ROOT_TEXT_ACTION_CUT,
    ROOT_TEXT_ACTION_PASTE,
    ROOT_TEXT_ACTION_UNDO,
    ROOT_TEXT_ACTION_REDO,
    ROOT_TEXT_ACTION_SAVE,
    ROOT_TEXT_ACTION_EXIT,

    ROOT_TEXT_ACTION_INTERRUPT,
    ROOT_TEXT_ACTION_CLEAR_SCREEN,
    ROOT_TEXT_ACTION_LINE_START,
    ROOT_TEXT_ACTION_LINE_END,
    ROOT_TEXT_ACTION_LINE_CLEAR,
    ROOT_TEXT_ACTION_SUSPEND,

    ROOT_TEXT_ACTION_TERMINAL_COPY,
    ROOT_TEXT_ACTION_TERMINAL_PASTE

} RootTextAction;

bool roottext_should_insert(
    const RootInputEvent* event
);

RootTextAction roottext_editor_action(
    const RootInputEvent* event
);

RootTextAction roottext_terminal_action(
    const RootInputEvent* event
);

#endif
