#ifndef TERMINAL_H
#define TERMINAL_H

#include "io.h"

void terminal_clear(void);

void terminal_putchar(char c);

void terminal_print(const char* text);

void terminal_set_cursor(
    u32 x,
    u32 y
);

u32 terminal_get_row(void);

u32 terminal_get_col(void);

#endif