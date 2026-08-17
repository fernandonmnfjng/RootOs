#ifndef ROOTOS_TERMINAL_H
#define ROOTOS_TERMINAL_H

#include "types.h"
#include "unicode.h"

void terminal_init(void);
void terminal_clear(void);
void terminal_clear_row(u32 row);

/* Batch many cell writes into one cursor/mouse update. */
void terminal_begin_batch(void);
void terminal_end_batch(void);

void terminal_putchar(char c);
void terminal_putcodepoint(RootCodepoint codepoint);
void terminal_print(const char* text);

void terminal_set_cursor(u32 x, u32 y);
u32 terminal_get_row(void);
u32 terminal_get_col(void);
u32 terminal_get_columns(void);
u32 terminal_get_rows(void);

u32 terminal_codepoint_cells(RootCodepoint codepoint);

bool terminal_pixel_to_cell(
    i32 pixel_x,
    i32 pixel_y,
    u32* column,
    u32* row
);

/* Visible terminal text selection. */
bool terminal_selection_begin(i32 pixel_x, i32 pixel_y);
bool terminal_selection_drag(i32 pixel_x, i32 pixel_y);
void terminal_selection_end(void);
void terminal_selection_clear(void);
bool terminal_selection_active(void);
bool terminal_selection_copy(void);
bool terminal_selection_select_word(i32 pixel_x, i32 pixel_y);

/* Fixed-size scrollback; no unbounded RAM growth. */
void terminal_scrollback_up(u32 lines);
void terminal_scrollback_down(u32 lines);
void terminal_scrollback_bottom(void);
u32 terminal_scrollback_available(void);

#endif
