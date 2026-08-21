#ifndef ROOTOS_IPV4_H
#define ROOTOS_IPV4_H

#include "types.h"

void ipv4_init(void);
void ipv4_receive(const u8* frame, usize size);

bool ipv4_send_packet(
    u32 destination_ip,
    u8 protocol,
    const void* payload,
    usize payload_size
);

bool ipv4_send_udp_broadcast(
    u16 source_port,
    u16 destination_port,
    const void* payload,
    usize payload_size
);

#endif
