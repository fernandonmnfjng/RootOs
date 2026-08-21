#include "net.h"

#include "net_device.h"
#include "arp.h"
#include "ipv4.h"
#include "udp.h"
#include "dhcp.h"
#include "dns.h"
#include "tcp.h"
#include "memory.h"
#include "time.h"

#pragma pack(push, 1)
typedef struct
{
    u8 destination[6];
    u8 source[6];
    u16 type;
} EthernetHeader;
#pragma pack(pop)

static RootNetConfig config;
static u8 receive_buffer[ROOT_NET_FRAME_MAX];
static u8 transmit_buffer[ROOT_NET_FRAME_MAX];

u16 net_read_be16(const u8* data)
{
    return ((u16)data[0] << 8) | (u16)data[1];
}

u32 net_read_be32(const u8* data)
{
    return
        ((u32)data[0] << 24) |
        ((u32)data[1] << 16) |
        ((u32)data[2] << 8) |
        (u32)data[3];
}

void net_write_be16(u8* data, u16 value)
{
    data[0] = (u8)(value >> 8);
    data[1] = (u8)(value & 0xFFu);
}

void net_write_be32(u8* data, u32 value)
{
    data[0] = (u8)(value >> 24);
    data[1] = (u8)((value >> 16) & 0xFFu);
    data[2] = (u8)((value >> 8) & 0xFFu);
    data[3] = (u8)(value & 0xFFu);
}

void net_init(void)
{
    root_memzero(&config, sizeof(config));

    config.adapter_ready = net_device_ready();
    config.link_up = net_device_link_up();

    if (config.adapter_ready)
        net_device_get_mac(config.mac);

    arp_init();
    ipv4_init();
    udp_init();
    dhcp_init();
    dns_init();
    tcp_init();
}

static void net_refresh_adapter(void)
{
    config.adapter_ready = net_device_ready();
    config.link_up = net_device_link_up();

    root_memzero(config.mac, sizeof(config.mac));
    if (config.adapter_ready)
        net_device_get_mac(config.mac);
}

const RootNetConfig* net_config(void)
{
    net_refresh_adapter();
    return &config;
}

bool net_select_device(usize index)
{
    if (!net_device_select(index))
        return false;

    net_clear_ipv4();
    net_refresh_adapter();
    return true;
}

bool net_send_ethernet(
    const u8 destination[6],
    u16 ether_type,
    const void* payload,
    usize payload_size
)
{
    if (
        !net_device_ready() ||
        destination == NULL ||
        (payload_size > 0u && payload == NULL) ||
        payload_size > ROOT_ETH_MTU ||
        sizeof(EthernetHeader) + payload_size > sizeof(transmit_buffer)
    )
    {
        return false;
    }

    net_refresh_adapter();

    EthernetHeader* header = (EthernetHeader*)transmit_buffer;
    root_memcpy(header->destination, destination, 6u);
    root_memcpy(header->source, config.mac, 6u);
    net_write_be16((u8*)&header->type, ether_type);

    if (payload_size > 0u)
    {
        root_memcpy(
            transmit_buffer + sizeof(EthernetHeader),
            payload,
            payload_size
        );
    }

    usize frame_size = sizeof(EthernetHeader) + payload_size;
    if (frame_size < 60u)
    {
        root_memzero(transmit_buffer + frame_size, 60u - frame_size);
        frame_size = 60u;
    }

    return net_device_send_frame(transmit_buffer, frame_size);
}

bool net_resolve_ipv4(
    u32 destination_ip,
    u8 destination_mac[6],
    u32 timeout_ms
)
{
    if (destination_mac == NULL || !config.configured || destination_ip == 0u)
        return false;

    if (destination_ip == 0xFFFFFFFFu)
    {
        for (u32 i = 0u; i < 6u; i++)
            destination_mac[i] = 0xFFu;
        return true;
    }

    u32 next_hop = destination_ip;
    if (
        config.subnet_mask != 0u &&
        (destination_ip & config.subnet_mask) !=
        (config.ipv4_address & config.subnet_mask)
    )
    {
        next_hop = config.gateway;
    }

    if (next_hop == 0u)
        return false;

    if (arp_lookup(next_hop, destination_mac))
        return true;

    if (!arp_request(next_hop))
        return false;

    if (timeout_ms == 0u)
        timeout_ms = 1000u;

    u64 deadline = root_time_millis() + timeout_ms;
    while (root_time_millis() < deadline)
    {
        net_poll();
        if (arp_lookup(next_hop, destination_mac))
            return true;
        __asm__ volatile ("hlt");
    }

    return false;
}

void net_poll(void)
{
    if (!net_device_ready())
        return;

    config.link_up = net_device_link_up();

    for (u32 packet = 0u; packet < 32u; packet++)
    {
        usize size = 0u;

        if (!net_device_receive_frame(receive_buffer, sizeof(receive_buffer), &size))
            break;

        if (size < sizeof(EthernetHeader))
            continue;

        const EthernetHeader* header = (const EthernetHeader*)receive_buffer;
        u16 type = net_read_be16((const u8*)&header->type);

        if (type == ROOT_ETH_TYPE_ARP)
            arp_receive(receive_buffer, size);
        else if (type == ROOT_ETH_TYPE_IPV4)
            ipv4_receive(receive_buffer, size);
    }
}

void net_apply_dhcp(
    u32 address,
    u32 subnet_mask,
    u32 gateway,
    u32 dns_server,
    u32 dhcp_server,
    u32 lease_seconds
)
{
    config.ipv4_address = address;
    config.subnet_mask = subnet_mask;
    config.gateway = gateway;
    config.dns_server = dns_server;
    config.dhcp_server = dhcp_server;
    config.lease_seconds = lease_seconds;
    config.configured = address != 0u;
    config.dhcp = config.configured;
}

void net_clear_ipv4(void)
{
    config.ipv4_address = 0u;
    config.subnet_mask = 0u;
    config.gateway = 0u;
    config.dns_server = 0u;
    config.dhcp_server = 0u;
    config.lease_seconds = 0u;
    config.configured = false;
    config.dhcp = false;
    dhcp_init();
    arp_init();
    dns_cache_flush();
    tcp_init();
}
