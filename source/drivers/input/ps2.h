#ifndef ROOTOS_PS2_H
#define ROOTOS_PS2_H

#include "types.h"


#define PS2_STATUS_OUTPUT_FULL 0x01
#define PS2_STATUS_INPUT_FULL  0x02
#define PS2_STATUS_MOUSE_DATA  0x20


u8 ps2_status(void);

bool ps2_has_data(void);

u8 ps2_read_data(void);


bool ps2_wait_input_clear(void);

bool ps2_wait_output_full(void);


bool ps2_write_command(
    u8 command
);

bool ps2_write_data(
    u8 data
);


bool ps2_write_mouse(
    u8 data
);


void ps2_flush(void);


#endif