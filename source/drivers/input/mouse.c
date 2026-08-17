#include "mouse.h"
#include "ps2.h"


#define MOUSE_ACK 0xFA


static u8 packet_bytes[3];

static u8 packet_index = 0;


/*
 * Esperar respuesta procedente
 * del dispositivo auxiliar.
 */
static bool mouse_wait_response(
    u8* response
)
{
    for (
        u32 i = 0;
        i < 1000000;
        i++
    )
    {
        u8 status =
            ps2_status();


        if (
            (
                status
                &
                PS2_STATUS_OUTPUT_FULL
            )
            ==
            0
        )
        {
            continue;
        }


        u8 value =
            ps2_read_data();


        /*
         * Bit 5:
         * byte procedente del mouse.
         */
        if (
            status
            &
            PS2_STATUS_MOUSE_DATA
        )
        {
            if (response)
            {
                *response =
                    value;
            }


            return true;
        }
    }


    return false;
}


static bool mouse_command(
    u8 command
)
{
    if (
        !ps2_write_mouse(
            command
        )
    )
    {
        return false;
    }


    u8 response;


    if (
        !mouse_wait_response(
            &response
        )
    )
    {
        return false;
    }


    return
        response
        ==
        MOUSE_ACK;
}


bool mouse_init(void)
{
    packet_index = 0;


    /*
     * Habilitar segundo puerto PS/2.
     */
    if (
        !ps2_write_command(
            0xA8
        )
    )
    {
        return false;
    }


    /*
     * Valores predeterminados.
     */
    if (
        !mouse_command(
            0xF6
        )
    )
    {
        return false;
    }


    /*
     * Activar envío de paquetes.
     */
    if (
        !mouse_command(
            0xF4
        )
    )
    {
        return false;
    }


    return true;
}


bool mouse_feed_byte(
    u8 data,
    MousePacket* packet
)
{
    if (
        packet == NULL
    )
    {
        return false;
    }


    /*
     * Primer byte de un paquete estándar
     * tiene siempre bit 3 activo.
     */
    if (
        packet_index == 0
        &&
        (
            data & 0x08
        )
        ==
        0
    )
    {
        return false;
    }


    packet_bytes[
        packet_index
    ] =
        data;


    packet_index++;


    if (
        packet_index < 3
    )
    {
        return false;
    }


    packet_index = 0;


    u8 flags =
        packet_bytes[0];


    /*
     * Overflow:
     * ignorar movimiento corrupto.
     */
    if (
        flags
        &
        0xC0
    )
    {
        packet->dx = 0;
        packet->dy = 0;
    }

    else
    {
        packet->dx =
            (i32)(
                (i8)
                packet_bytes[1]
            );


        /*
         * PS/2 positivo Y = arriba.
         * Pantalla positivo Y = abajo.
         */
        packet->dy =
            -
            (i32)(
                (i8)
                packet_bytes[2]
            );
    }


    packet->left =
        (
            flags
            &
            0x01
        )
        !=
        0;


    packet->right =
        (
            flags
            &
            0x02
        )
        !=
        0;


    packet->middle =
        (
            flags
            &
            0x04
        )
        !=
        0;


    return true;
}