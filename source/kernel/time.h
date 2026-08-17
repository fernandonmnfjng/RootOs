#ifndef ROOTOS_TIME_H
#define ROOTOS_TIME_H

#include "types.h"


u64 root_time_millis(void);


u64 root_time_seconds(void);


void root_sleep_ms(
    u64 milliseconds
);


#endif