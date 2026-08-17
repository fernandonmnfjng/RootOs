#ifndef ROOTOS_INTERRUPTS_H
#define ROOTOS_INTERRUPTS_H

#include "types.h"


void interrupts_init(void);


void interrupts_enable(void);

void interrupts_disable(void);


bool interrupts_enabled(void);


#endif