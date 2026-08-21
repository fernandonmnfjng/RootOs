#ifndef ROOTOS_NET_H
#define ROOTOS_NET_H

#include "types.h"

#define ROOT_ETH_ADDR_LEN 6u
#define ROOT_ETH_MTU 1500u
#define ROOT_NET_FRAME_MAX 2048u

#define ROOT_ETH_TYPE_IPV4 0x0800u
#define ROOT_ETH_TYPE_ARP  0x0806u

#define ROOT_IPV4(a,b,c,d) \
    ((((u32)(a) & 0xFFu) << 24) | \
     (((u32)(b) & 0xFFu) << 16) | \
     (((u32)(c) & 0xFFu) << 8)  | \
     ((u32)(d) & 0xFFu))

typedef struct
{
    bool adapter_ready;
    bool link_up;
    bool configured;
    bool dhcp;

    u8 mac[ROOT_ETH_ADDR_LEN];
    u32 ipv4_address;
    u32 subnet_mask;
    u32 gateway;
    u32 dns_server;
    u32 dhcp_server;
    u32 lease_seconds;
} RootNetConfig;

void net_init(void);
void net_poll(void);
bool net_select_device(usize index);

const RootNetConfig* net_config(void);

bool net_send_ethernet(
    const u8 destination[ROOT_ETH_ADDR_LEN],
    u16 ether_type,
    const void* payload,
    usize payload_size
);

/* Resolve the Ethernet next-hop for an IPv4 destination.  Local destinations
 * resolve directly; off-subnet traffic resolves the configured gateway. */
bool net_resolve_ipv4(
    u32 destination_ip,
    u8 destination_mac[ROOT_ETH_ADDR_LEN],
    u32 timeout_ms
);

void net_apply_dhcp(
    u32 address,
    u32 subnet_mask,
    u32 gateway,
    u32 dns_server,
    u32 dhcp_server,
    u32 lease_seconds
);

void net_clear_ipv4(void);

u16 net_read_be16(const u8* data);
u32 net_read_be32(const u8* data);
void net_write_be16(u8* data, u16 value);
void net_write_be32(u8* data, u32 value);

#endif
