#include "roottext.h"

static bool is_key_down(const RootInputEvent* event)
{
    return event != NULL && event->type == ROOT_INPUT_KEY_DOWN;
}

static bool plain_ctrl(const RootInputEvent* event)
{
    if (!is_key_down(event))
        return false;

    if (event->altgr)
        return false;

    return event->ctrl && !event->alt;
}

bool roottext_should_insert(
    const RootInputEvent* event
)
{
    if (!is_key_down(event) || event->codepoint == 0)
        return false;

    /* AltGr is a text-producing modifier on LATAM layouts. */
    if (event->altgr)
        return true;

    /* Ctrl and Alt are command modifiers, never text input. */
    if (event->ctrl || event->alt)
        return false;

    return true;
}

RootTextAction roottext_editor_action(
    const RootInputEvent* event
)
{
    if (!plain_ctrl(event))
        return ROOT_TEXT_ACTION_NONE;

    switch (event->key)
    {
        case ROOT_KEY_A: return ROOT_TEXT_ACTION_SELECT_ALL;
        case ROOT_KEY_C: return ROOT_TEXT_ACTION_COPY;
        case ROOT_KEY_X: return ROOT_TEXT_ACTION_CUT;
        case ROOT_KEY_V: return ROOT_TEXT_ACTION_PASTE;
        case ROOT_KEY_Z:
            return event->shift
                ? ROOT_TEXT_ACTION_REDO
                : ROOT_TEXT_ACTION_UNDO;

        case ROOT_KEY_Y: return ROOT_TEXT_ACTION_REDO;
        case ROOT_KEY_S: return ROOT_TEXT_ACTION_SAVE;
        case ROOT_KEY_Q: return ROOT_TEXT_ACTION_EXIT;
        default: return ROOT_TEXT_ACTION_NONE;
    }
}

RootTextAction roottext_terminal_action(
    const RootInputEvent* event
)
{
    if (!is_key_down(event) || event->altgr)
        return ROOT_TEXT_ACTION_NONE;

    if (event->ctrl && event->shift && !event->alt)
    {
        if (event->key == ROOT_KEY_C)
            return ROOT_TEXT_ACTION_TERMINAL_COPY;

        if (event->key == ROOT_KEY_V)
            return ROOT_TEXT_ACTION_TERMINAL_PASTE;

        return ROOT_TEXT_ACTION_NONE;
    }

    if (!event->ctrl || event->alt)
        return ROOT_TEXT_ACTION_NONE;

    switch (event->key)
    {
        case ROOT_KEY_C: return ROOT_TEXT_ACTION_INTERRUPT;
        case ROOT_KEY_L: return ROOT_TEXT_ACTION_CLEAR_SCREEN;
        case ROOT_KEY_A: return ROOT_TEXT_ACTION_LINE_START;
        case ROOT_KEY_E: return ROOT_TEXT_ACTION_LINE_END;
        case ROOT_KEY_U: return ROOT_TEXT_ACTION_LINE_CLEAR;
        case ROOT_KEY_Z: return ROOT_TEXT_ACTION_SUSPEND;
        default: return ROOT_TEXT_ACTION_NONE;
    }
}
