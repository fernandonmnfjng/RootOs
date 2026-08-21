#ifndef ROOTOS_TCP_H
#define ROOTOS_TCP_H

#include "types.h"

#define ROOT_TCP_MAX_CONNECTIONS 4u

typedef enum
{
    ROOT_TCP_CLOSED = 0,
    ROOT_TCP_SYN_SENT,
    ROOT_TCP_ESTABLISHED,
    ROOT_TCP_FIN_WAIT,
    ROOT_TCP_RESET,
    ROOT_TCP_ERROR
} RootTcpState;

typedef struct
{
    bool used;
    u32 remote_ip;
    u16 local_port;
    u16 remote_port;
    u32 send_next;
    u32 receive_next;
    RootTcpState state;
    const char* last_error;
} RootTcpConnection;

void tcp_init(void);
int tcp_connect(u32 remote_ip, u16 remote_port, u32 timeout_ms);
bool tcp_close(int id);
RootTcpState tcp_state(int id);
const char* tcp_state_name(RootTcpState state);
const char* tcp_last_error(int id);
usize tcp_connection_count(void);
bool tcp_get_connection(usize index, RootTcpConnection* output, int* id);

void tcp_receive(
    u32 source_ip,
    u32 destination_ip,
    const u8* segment,
    usize segment_size
);

#endif
