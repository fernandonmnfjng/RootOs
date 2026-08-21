#ifndef ROOTOS_MOUSE_H
#define ROOTOS_MOUSE_H

#include "types.h"

typedef struct
{
    i32 dx;
    i32 dy;
    i32 wheel;

    bool left;
    bool right;
    bool middle;

} MousePacket;

/*
 * Initialize the PS/2 mouse.  RootOS attempts to enable the standard
 * IntelliMouse extension (device ID 3) so a fourth packet byte carries
 * wheel movement.  Plain 3-byte PS/2 mice remain supported.
 */
bool mouse_init(void);

/* Feed one byte read from the PS/2 auxiliary port. */
bool mouse_feed_byte(
    u8 data,
    MousePacket* packet
);

bool mouse_wheel_available(void);

#endif
