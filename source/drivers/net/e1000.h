#ifndef ROOTOS_E1000_H
#define ROOTOS_E1000_H

#include "types.h"
#include "pci.h"

void e1000_init(void);
bool e1000_ready(void);
bool e1000_link_up(void);

bool e1000_send_frame(const void* data, usize size);
bool e1000_receive_frame(void* output, usize capacity, usize* result_size);

void e1000_get_mac(u8 output[6]);
const PciDevice* e1000_pci_device(void);
const char* e1000_last_error(void);

#endif
