#ifndef ROOTOS_ROOTDISPLAY_H
#define ROOTOS_ROOTDISPLAY_H

#include "types.h"
#include "multiboot.h"


/*
 * ============================================================
 * INITIALIZATION
 * ============================================================
 */

void rootdisplay_init(
    const MultibootInfo* multiboot
);


bool rootdisplay_ready(void);


/*
 * ============================================================
 * DISPLAY INFORMATION
 * ============================================================
 */

u32 rootdisplay_width(void);

u32 rootdisplay_height(void);


/*
 * ============================================================
 * COLOR
 * ============================================================
 */

u32 rootdisplay_rgb(
    u8 red,
    u8 green,
    u8 blue
);


/*
 * ============================================================
 * PIXELS
 * ============================================================
 */

void rootdisplay_put_pixel(
    u32 x,
    u32 y,
    u32 color
);


u32 rootdisplay_get_pixel(
    u32 x,
    u32 y
);


/*
 * ============================================================
 * RECTANGLES
 * ============================================================
 */

void rootdisplay_fill_rect(
    u32 x,
    u32 y,
    u32 width,
    u32 height,
    u32 color
);


void rootdisplay_invert_rect(
    u32 x,
    u32 y,
    u32 width,
    u32 height
);


/*
 * ============================================================
 * BULK UPDATE
 * ============================================================
 *
 * Permite realizar cientos/miles de operaciones de dibujo
 * escondiendo/restaurando el cursor UNA sola vez.
 */

void rootdisplay_begin_update(void);

void rootdisplay_end_update(void);


/*
 * ============================================================
 * CLEAR
 * ============================================================
 */

void rootdisplay_clear(
    u32 color
);


/*
 * ============================================================
 * SOFTWARE MOUSE CURSOR
 * ============================================================
 */

void rootdisplay_cursor_enable(
    bool enabled
);


void rootdisplay_cursor_move(
    i32 x,
    i32 y
);


bool rootdisplay_cursor_visible(void);


#endif