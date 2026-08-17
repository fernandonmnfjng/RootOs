#ifndef ROOTOS_PIT_H
#define ROOTOS_PIT_H

#include "types.h"


#define PIT_DEFAULT_FREQUENCY 1000


void pit_init(
    u32 frequency
);


void pit_irq_handler(void);


u64 pit_ticks(void);

u64 pit_millis(void);


u32 pit_frequency(void);


#endif