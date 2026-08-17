#ifndef ROOTOS_TERMINAL_H
#define ROOTOS_TERMINAL_H

#include "types.h"
#include "unicode.h"


void terminal_init(void);


void terminal_clear(void);


void terminal_putchar(
    char c
);


void terminal_putcodepoint(
    RootCodepoint codepoint
);


void terminal_print(
    const char* text
);


void terminal_set_cursor(
    u32 x,
    u32 y
);

u32 terminal_get_row(void);

u32 terminal_get_col(void);

u32 terminal_get_columns(void);

u32 terminal_get_rows(void);


u32 terminal_codepoint_cells(
    RootCodepoint codepoint
);


bool terminal_pixel_to_cell(
    i32 pixel_x,
    i32 pixel_y,
    u32* column,
    u32* row
);
#endif