#ifndef ROOTOS_UDP_H
#define ROOTOS_UDP_H

#include "types.h"

#define ROOT_UDP_MAX_BINDINGS 8u
#define ROOT_UDP_EPHEMERAL_FIRST 49152u
#define ROOT_UDP_EPHEMERAL_LAST  65535u

typedef void (*RootUdpReceiveHandler)(
    u32 source_ip,
    u16 source_port,
    u16 destination_port,
    const u8* payload,
    usize payload_size,
    void* context
);

void udp_init(void);

bool udp_bind(
    u16 port,
    RootUdpReceiveHandler handler,
    void* context
);

void udp_unbind(u16 port);
bool udp_port_bound(u16 port);
u16 udp_allocate_ephemeral_port(void);

bool udp_send_to(
    u32 destination_ip,
    u16 source_port,
    u16 destination_port,
    const void* payload,
    usize payload_size
);

bool udp_send_broadcast(
    u16 source_port,
    u16 destination_port,
    const void* payload,
    usize payload_size
);

void udp_receive(
    u32 source_ip,
    u32 destination_ip,
    const u8* segment,
    usize segment_size
);

#endif
