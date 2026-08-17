#include "pic.h"
#include "io.h"


#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21

#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1


#define PIC_EOI 0x20


/*
 * Pequeña espera para hardware antiguo.
 */
static void pic_io_wait(void)
{
    outb(
        0x80,
        0
    );
}


void pic_remap(
    u8 master_offset,
    u8 slave_offset
)
{
    /*
     * Guardar máscaras actuales.
     */
    u8 master_mask =
        inb(
            PIC1_DATA
        );

    u8 slave_mask =
        inb(
            PIC2_DATA
        );


    /*
     * ICW1:
     * comenzar inicialización.
     */
    outb(
        PIC1_COMMAND,
        0x11
    );

    pic_io_wait();


    outb(
        PIC2_COMMAND,
        0x11
    );

    pic_io_wait();


    /*
     * ICW2:
     * offsets de interrupciones.
     *
     * Master:
     * 0x20 - 0x27
     *
     * Slave:
     * 0x28 - 0x2F
     */
    outb(
        PIC1_DATA,
        master_offset
    );

    pic_io_wait();


    outb(
        PIC2_DATA,
        slave_offset
    );

    pic_io_wait();


    /*
     * ICW3:
     *
     * slave conectado a IRQ2.
     */
    outb(
        PIC1_DATA,
        0x04
    );

    pic_io_wait();


    outb(
        PIC2_DATA,
        0x02
    );

    pic_io_wait();


    /*
     * ICW4:
     * modo 8086.
     */
    outb(
        PIC1_DATA,
        0x01
    );

    pic_io_wait();


    outb(
        PIC2_DATA,
        0x01
    );

    pic_io_wait();


    /*
     * Restaurar máscaras.
     */
    outb(
        PIC1_DATA,
        master_mask
    );

    outb(
        PIC2_DATA,
        slave_mask
    );
}


void pic_mask_all(void)
{
    outb(
        PIC1_DATA,
        0xFF
    );

    outb(
        PIC2_DATA,
        0xFF
    );
}


void pic_mask_irq(
    u8 irq
)
{
    u16 port;

    u8 bit;


    if (
        irq < 8
    )
    {
        port =
            PIC1_DATA;

        bit =
            irq;
    }

    else
    {
        port =
            PIC2_DATA;

        bit =
            irq - 8;
    }


    u8 value =
        inb(
            port
        );


    value |=
        (
            1u
            <<
            bit
        );


    outb(
        port,
        value
    );
}


void pic_unmask_irq(
    u8 irq
)
{
    u16 port;

    u8 bit;


    if (
        irq < 8
    )
    {
        port =
            PIC1_DATA;

        bit =
            irq;
    }

    else
    {
        /*
         * Necesitamos además IRQ2
         * del PIC maestro para llegar
         * al PIC esclavo.
         */
        u8 master =
            inb(
                PIC1_DATA
            );


        master &=
            ~(
                1u
                <<
                2
            );


        outb(
            PIC1_DATA,
            master
        );


        port =
            PIC2_DATA;

        bit =
            irq - 8;
    }


    u8 value =
        inb(
            port
        );


    value &=
        ~(
            1u
            <<
            bit
        );


    outb(
        port,
        value
    );
}


void pic_send_eoi(
    u8 irq
)
{
    if (
        irq >= 8
    )
    {
        outb(
            PIC2_COMMAND,
            PIC_EOI
        );
    }


    outb(
        PIC1_COMMAND,
        PIC_EOI
    );
}