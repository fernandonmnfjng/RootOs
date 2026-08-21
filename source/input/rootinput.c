/*
 * RootOS v0.47.6-3 input compatibility wrapper.
 *
 * Keep the complete existing RootInput implementation, but do not tie the
 * visibility of the software pointer to successful PS/2 mouse negotiation.
 * Modern machines may have no 8042 mouse at all; the pointer must still exist
 * so USB HID can become another motion backend without owning presentation.
 */

#define rootinput_init legacy_rootinput_init
#include "rootinput_legacy_impl.c"
#undef rootinput_init

void rootinput_init(void)
{
    legacy_rootinput_init();

    if (rootdisplay_ready())
    {
        /*
         * Always expose the RootOS software pointer. If PS/2 is available the
         * legacy backend moves it immediately. If PS/2 is absent it remains
         * centered until another input backend (USB HID in 0.48) feeds motion.
         */
        rootdisplay_cursor_move(
            mouse_x,
            mouse_y
        );

        rootdisplay_cursor_enable(
            true
        );
    }
}
