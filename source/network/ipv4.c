#include "ipv4.h"

#include "net.h"
#include "udp.h"
#include "tcp.h"
#include "memory.h"

#define IPV4_PROTOCOL_TCP 6u
#define IPV4_PROTOCOL_UDP 17u

#pragma pack(push, 1)
typedef struct
{
    u8 version_ihl;
    u8 dscp_ecn;
    u16 total_length;
    u16 identification;
    u16 flags_fragment;
    u8 ttl;
    u8 protocol;
    u16 checksum;
    u8 source[4];
    u8 destination[4];
} Ipv4Header;

typedef struct
{
    u16 source_port;
    u16 destination_port;
    u16 length;
    u16 checksum;
} UdpHeader;
#pragma pack(pop)

static u16 next_identification = 1u;
static u8 packet_buffer[ROOT_ETH_MTU];

static u16 ipv4_checksum(const void* data, usize size)
{
    const u8* bytes = (const u8*)data;
    u32 sum = 0u;

    for (usize i = 0u; i + 1u < size; i += 2u)
    {
        sum += ((u16)bytes[i] << 8) | (u16)bytes[i + 1u];
        while ((sum >> 16) != 0u)
            sum = (sum & 0xFFFFu) + (sum >> 16);
    }

    if ((size & 1u) != 0u)
    {
        sum += (u16)bytes[size - 1u] << 8;
        while ((sum >> 16) != 0u)
            sum = (sum & 0xFFFFu) + (sum >> 16);
    }

    return (u16)(~sum & 0xFFFFu);
}

static void build_header(
    Ipv4Header* ip,
    usize total_size,
    u8 protocol,
    u32 source_ip,
    u32 destination_ip
)
{
    ip->version_ihl = 0x45u;
    ip->dscp_ecn = 0u;
    net_write_be16((u8*)&ip->total_length, (u16)total_size);
    net_write_be16((u8*)&ip->identification, next_identification++);
    net_write_be16((u8*)&ip->flags_fragment, 0x4000u); /* Don't fragment. */
    ip->ttl = 64u;
    ip->protocol = protocol;
    net_write_be16((u8*)&ip->checksum, 0u);
    net_write_be32(ip->source, source_ip);
    net_write_be32(ip->destination, destination_ip);
    net_write_be16((u8*)&ip->checksum, ipv4_checksum(ip, sizeof(Ipv4Header)));
}

void ipv4_init(void)
{
    next_identification = 1u;
}

bool ipv4_send_packet(
    u32 destination_ip,
    u8 protocol,
    const void* payload,
    usize payload_size
)
{
    const RootNetConfig* config = net_config();
    usize total_size = sizeof(Ipv4Header) + payload_size;

    if (
        !config->configured ||
        destination_ip == 0u ||
        (payload_size > 0u && payload == NULL) ||
        total_size > sizeof(packet_buffer)
    )
    {
        return false;
    }

    u8 destination_mac[6];
    if (!net_resolve_ipv4(destination_ip, destination_mac, 1000u))
        return false;

    root_memzero(packet_buffer, total_size);
    Ipv4Header* ip = (Ipv4Header*)packet_buffer;
    build_header(
        ip,
        total_size,
        protocol,
        config->ipv4_address,
        destination_ip
    );

    if (payload_size > 0u)
        root_memcpy(packet_buffer + sizeof(Ipv4Header), payload, payload_size);

    return net_send_ethernet(
        destination_mac,
        ROOT_ETH_TYPE_IPV4,
        packet_buffer,
        total_size
    );
}

bool ipv4_send_udp_broadcast(
    u16 source_port,
    u16 destination_port,
    const void* payload,
    usize payload_size
)
{
    usize ip_size = sizeof(Ipv4Header) + sizeof(UdpHeader) + payload_size;

    if (
        (payload_size > 0u && payload == NULL) ||
        ip_size > sizeof(packet_buffer)
    )
    {
        return false;
    }

    root_memzero(packet_buffer, ip_size);

    Ipv4Header* ip = (Ipv4Header*)packet_buffer;
    UdpHeader* udp = (UdpHeader*)(packet_buffer + sizeof(Ipv4Header));
    u8* udp_payload = packet_buffer + sizeof(Ipv4Header) + sizeof(UdpHeader);

    /* DHCP starts before an address is configured, so broadcast supports
     * source 0.0.0.0 and deliberately leaves the UDP checksum disabled. */
    u32 source_ip = net_config()->configured ? net_config()->ipv4_address : 0u;
    build_header(ip, ip_size, IPV4_PROTOCOL_UDP, source_ip, 0xFFFFFFFFu);

    net_write_be16((u8*)&udp->source_port, source_port);
    net_write_be16((u8*)&udp->destination_port, destination_port);
    net_write_be16((u8*)&udp->length, (u16)(sizeof(UdpHeader) + payload_size));
    net_write_be16((u8*)&udp->checksum, 0u);

    if (payload_size > 0u)
        root_memcpy(udp_payload, payload, payload_size);

    static const u8 broadcast[6] =
        { 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu };

    return net_send_ethernet(broadcast, ROOT_ETH_TYPE_IPV4, packet_buffer, ip_size);
}

void ipv4_receive(const u8* frame, usize size)
{
    if (frame == NULL || size < 14u + sizeof(Ipv4Header))
        return;

    const u8* packet = frame + 14u;
    usize packet_available = size - 14u;
    const Ipv4Header* ip = (const Ipv4Header*)packet;

    if ((ip->version_ihl >> 4) != 4u)
        return;

    usize header_size = (usize)(ip->version_ihl & 0x0Fu) * 4u;
    if (header_size < 20u || header_size > packet_available)
        return;

    usize total_length = net_read_be16((const u8*)&ip->total_length);
    if (total_length < header_size || total_length > packet_available)
        return;

    if (ipv4_checksum(ip, header_size) != 0u)
        return;

    u16 fragment = net_read_be16((const u8*)&ip->flags_fragment);
    if ((fragment & 0x3FFFu) != 0u)
        return; /* Fragment reassembly is not part of v0.47. */

    u32 source_ip = net_read_be32(ip->source);
    u32 destination_ip = net_read_be32(ip->destination);
    const RootNetConfig* config = net_config();

    if (
        config->configured &&
        destination_ip != config->ipv4_address &&
        destination_ip != 0xFFFFFFFFu
    )
    {
        return;
    }

    const u8* payload = packet + header_size;
    usize payload_size = total_length - header_size;

    if (ip->protocol == IPV4_PROTOCOL_UDP)
    {
        udp_receive(source_ip, destination_ip, payload, payload_size);
    }
    else if (ip->protocol == IPV4_PROTOCOL_TCP)
    {
        tcp_receive(source_ip, destination_ip, payload, payload_size);
    }
}
