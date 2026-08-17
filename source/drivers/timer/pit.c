#include "pit.h"

#include "io.h"
#include "pic.h"


#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43

#define PIT_BASE_FREQUENCY 1193182


static volatile u64 ticks =
    0;


static u32 current_frequency =
    PIT_DEFAULT_FREQUENCY;


/*
 * ============================================================
 * INITIALIZE PIT
 * ============================================================
 */

void pit_init(
    u32 frequency
)
{
    /*
     * Un divisor PIT es de 16 bits.
     */
    if (
        frequency < 19
    )
    {
        frequency = 19;
    }


    if (
        frequency
        >
        PIT_BASE_FREQUENCY
    )
    {
        frequency =
            PIT_BASE_FREQUENCY;
    }


    u32 divisor =
        PIT_BASE_FREQUENCY
        /
        frequency;


    if (
        divisor == 0
    )
    {
        divisor = 1;
    }


    if (
        divisor > 65535
    )
    {
        divisor =
            65535;
    }


    /*
     * Guardamos la frecuencia REAL
     * resultante del divisor.
     */
    current_frequency =
        PIT_BASE_FREQUENCY
        /
        divisor;


    ticks = 0;


    /*
     * Channel 0
     * Access lo/hi
     * Mode 3
     * Binary
     */
    outb(
        PIT_COMMAND,
        0x36
    );


    outb(
        PIT_CHANNEL0,
        (
            u8
        )(
            divisor
            &
            0xFF
        )
    );


    outb(
        PIT_CHANNEL0,
        (
            u8
        )(
            (
                divisor
                >>
                8
            )
            &
            0xFF
        )
    );


    /*
     * IRQ0.
     */
    pic_unmask_irq(
        0
    );
}


/*
 * ============================================================
 * INTERRUPT HANDLER
 * ============================================================
 */

void pit_irq_handler(void)
{
    ticks++;


    pic_send_eoi(
        0
    );
}


/*
 * ============================================================
 * TIME
 * ============================================================
 */

u64 pit_ticks(void)
{
    /*
     * En x86 de 32 bits una lectura de u64
     * no es necesariamente atómica.
     *
     * Leemos hasta obtener dos valores iguales.
     */

    u64 first;

    u64 second;


    do
    {
        first =
            ticks;

        second =
            ticks;

    }
    while (
        first != second
    );


    return first;
}


u64 pit_millis(void)
{
    u64 current_ticks =
        pit_ticks();


    return
        (
            current_ticks
            *
            1000ULL
        )
        /
        current_frequency;
}


u32 pit_frequency(void)
{
    return current_frequency;
}