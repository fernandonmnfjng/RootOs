#ifndef ROOTOS_DHCP_H
#define ROOTOS_DHCP_H

#include "types.h"

typedef enum
{
    DHCP_STATE_IDLE = 0,
    DHCP_STATE_DISCOVERING,
    DHCP_STATE_REQUESTING,
    DHCP_STATE_BOUND,
    DHCP_STATE_FAILED
} DhcpState;

void dhcp_init(void);
bool dhcp_acquire(void);
void dhcp_receive(const u8* payload, usize size);

DhcpState dhcp_state(void);
const char* dhcp_state_name(DhcpState state);
const char* dhcp_last_error(void);

#endif
