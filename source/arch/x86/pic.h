#ifndef ROOTOS_PIC_H
#define ROOTOS_PIC_H

#include "types.h"


void pic_remap(
    u8 master_offset,
    u8 slave_offset
);


void pic_mask_all(void);


void pic_mask_irq(
    u8 irq
);


void pic_unmask_irq(
    u8 irq
);


void pic_send_eoi(
    u8 irq
);


#endif