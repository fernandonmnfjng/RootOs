#ifndef ROOTOS_MOUSE_H
#define ROOTOS_MOUSE_H

#include "types.h"


typedef struct
{
    i32 dx;
    i32 dy;

    bool left;
    bool right;
    bool middle;

} MousePacket;


bool mouse_init(void);


bool mouse_feed_byte(
    u8 data,
    MousePacket* packet
);


#endif