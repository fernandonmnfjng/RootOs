#include "time.h"

#include "pit.h"

#include "interrupts.h"


u64 root_time_millis(void)
{
    return pit_millis();
}


u64 root_time_seconds(void)
{
    return
        root_time_millis()
        /
        1000ULL;
}


void root_sleep_ms(
    u64 milliseconds
)
{
    if (
        milliseconds == 0
    )
    {
        return;
    }


    u64 target =
        root_time_millis()
        +
        milliseconds;


    while (
        root_time_millis()
        <
        target
    )
    {
        /*
         * Con interrupciones activas,
         * no gastamos CPU esperando.
         */
        if (
            interrupts_enabled()
        )
        {
            __asm__ volatile(
                "hlt"
            );
        }
    }
}