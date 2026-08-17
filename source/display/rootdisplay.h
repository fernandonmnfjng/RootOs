#ifndef ROOTOS_ROOTDISPLAY_H
#define ROOTOS_ROOTDISPLAY_H

#include "types.h"
#include "multiboot.h"


bool rootdisplay_init(
    const MultibootInfo* multiboot
);


bool rootdisplay_ready(void);


u32 rootdisplay_width(void);

u32 rootdisplay_height(void);


u32 rootdisplay_rgb(
    u8 red,
    u8 green,
    u8 blue
);


void rootdisplay_put_pixel(
    u32 x,
    u32 y,
    u32 color
);


void rootdisplay_fill_rect(
    u32 x,
    u32 y,
    u32 width,
    u32 height,
    u32 color
);


void rootdisplay_clear(
    u32 color
);


#endif
