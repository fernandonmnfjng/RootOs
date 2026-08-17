#include "ps2.h"
#include "io.h"


#define PS2_DATA_PORT    0x60
#define PS2_STATUS_PORT  0x64
#define PS2_COMMAND_PORT 0x64

#define PS2_TIMEOUT 1000000


u8 ps2_status(void)
{
    return inb(
        PS2_STATUS_PORT
    );
}


bool ps2_has_data(void)
{
    return
        (
            ps2_status()
            &
            PS2_STATUS_OUTPUT_FULL
        )
        !=
        0;
}


u8 ps2_read_data(void)
{
    return inb(
        PS2_DATA_PORT
    );
}


bool ps2_wait_input_clear(void)
{
    for (
        u32 i = 0;
        i < PS2_TIMEOUT;
        i++
    )
    {
        if (
            (
                ps2_status()
                &
                PS2_STATUS_INPUT_FULL
            )
            ==
            0
        )
        {
            return true;
        }
    }


    return false;
}


bool ps2_wait_output_full(void)
{
    for (
        u32 i = 0;
        i < PS2_TIMEOUT;
        i++
    )
    {
        if (
            ps2_has_data()
        )
        {
            return true;
        }
    }


    return false;
}


bool ps2_write_command(
    u8 command
)
{
    if (
        !ps2_wait_input_clear()
    )
    {
        return false;
    }


    outb(
        PS2_COMMAND_PORT,
        command
    );


    return true;
}


bool ps2_write_data(
    u8 data
)
{
    if (
        !ps2_wait_input_clear()
    )
    {
        return false;
    }


    outb(
        PS2_DATA_PORT,
        data
    );


    return true;
}


bool ps2_write_mouse(
    u8 data
)
{
    /*
     * 0xD4:
     *
     * El siguiente byte enviado a 0x60
     * pertenece al segundo dispositivo
     * PS/2 (mouse).
     */

    if (
        !ps2_write_command(
            0xD4
        )
    )
    {
        return false;
    }


    return ps2_write_data(
        data
    );
}


void ps2_flush(void)
{
    for (
        u32 i = 0;
        i < 256;
        i++
    )
    {
        if (
            !ps2_has_data()
        )
        {
            break;
        }


        (void)ps2_read_data();
    }
}