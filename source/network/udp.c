#include "udp.h"

#include "ipv4.h"
#include "net.h"
#include "memory.h"
#include "time.h"

#define IPV4_PROTOCOL_UDP 17u

#pragma pack(push, 1)
typedef struct
{
    u16 source_port;
    u16 destination_port;
    u16 length;
    u16 checksum;
} UdpHeader;
#pragma pack(pop)

typedef struct
{
    bool used;
    u16 port;
    RootUdpReceiveHandler handler;
    void* context;
} UdpBinding;

static UdpBinding bindings[ROOT_UDP_MAX_BINDINGS];
static u16 next_ephemeral = ROOT_UDP_EPHEMERAL_FIRST;
static u8 transmit_buffer[ROOT_ETH_MTU];

static u32 checksum_add(const u8* data, usize size, u32 sum)
{
    if (data == NULL)
        return sum;

    usize i = 0u;
    while (i + 1u < size)
    {
        sum += ((u16)data[i] << 8) | (u16)data[i + 1u];
        i += 2u;
    }

    if (i < size)
        sum += (u16)data[i] << 8;

    while ((sum >> 16) != 0u)
        sum = (sum & 0xFFFFu) + (sum >> 16);

    return sum;
}

static u16 udp_checksum(
    u32 source_ip,
    u32 destination_ip,
    const u8* segment,
    usize segment_size
)
{
    u8 pseudo[12];
    net_write_be32(pseudo + 0u, source_ip);
    net_write_be32(pseudo + 4u, destination_ip);
    pseudo[8] = 0u;
    pseudo[9] = IPV4_PROTOCOL_UDP;
    net_write_be16(pseudo + 10u, (u16)segment_size);

    u32 sum = 0u;
    sum = checksum_add(pseudo, sizeof(pseudo), sum);
    sum = checksum_add(segment, segment_size, sum);

    u16 result = (u16)(~sum & 0xFFFFu);
    return result == 0u ? 0xFFFFu : result;
}

void udp_init(void)
{
    root_memzero(bindings, sizeof(bindings));
    next_ephemeral = ROOT_UDP_EPHEMERAL_FIRST;
}

bool udp_port_bound(u16 port)
{
    if (port == 0u)
        return false;

    for (usize i = 0u; i < ROOT_UDP_MAX_BINDINGS; i++)
    {
        if (bindings[i].used && bindings[i].port == port)
            return true;
    }

    return false;
}

bool udp_bind(u16 port, RootUdpReceiveHandler handler, void* context)
{
    if (port == 0u || handler == NULL || udp_port_bound(port))
        return false;

    for (usize i = 0u; i < ROOT_UDP_MAX_BINDINGS; i++)
    {
        if (!bindings[i].used)
        {
            bindings[i].used = true;
            bindings[i].port = port;
            bindings[i].handler = handler;
            bindings[i].context = context;
            return true;
        }
    }

    return false;
}

void udp_unbind(u16 port)
{
    for (usize i = 0u; i < ROOT_UDP_MAX_BINDINGS; i++)
    {
        if (bindings[i].used && bindings[i].port == port)
        {
            root_memzero(&bindings[i], sizeof(bindings[i]));
            return;
        }
    }
}

u16 udp_allocate_ephemeral_port(void)
{
    u32 range = (u32)ROOT_UDP_EPHEMERAL_LAST - (u32)ROOT_UDP_EPHEMERAL_FIRST + 1u;

    for (u32 attempt = 0u; attempt < range; attempt++)
    {
        u16 candidate = next_ephemeral;

        if (next_ephemeral == ROOT_UDP_EPHEMERAL_LAST)
            next_ephemeral = ROOT_UDP_EPHEMERAL_FIRST;
        else
            next_ephemeral++;

        if (!udp_port_bound(candidate))
            return candidate;
    }

    return 0u;
}

bool udp_send_to(
    u32 destination_ip,
    u16 source_port,
    u16 destination_port,
    const void* payload,
    usize payload_size
)
{
    const RootNetConfig* config = net_config();
    usize segment_size = sizeof(UdpHeader) + payload_size;

    if (
        !config->configured ||
        destination_ip == 0u ||
        destination_ip == 0xFFFFFFFFu ||
        source_port == 0u ||
        destination_port == 0u ||
        (payload_size > 0u && payload == NULL) ||
        segment_size > sizeof(transmit_buffer)
    )
    {
        return false;
    }

    root_memzero(transmit_buffer, segment_size);

    UdpHeader* header = (UdpHeader*)transmit_buffer;
    net_write_be16((u8*)&header->source_port, source_port);
    net_write_be16((u8*)&header->destination_port, destination_port);
    net_write_be16((u8*)&header->length, (u16)segment_size);
    net_write_be16((u8*)&header->checksum, 0u);

    if (payload_size > 0u)
        root_memcpy(transmit_buffer + sizeof(UdpHeader), payload, payload_size);

    u16 checksum = udp_checksum(
        config->ipv4_address,
        destination_ip,
        transmit_buffer,
        segment_size
    );
    net_write_be16((u8*)&header->checksum, checksum);

    return ipv4_send_packet(
        destination_ip,
        IPV4_PROTOCOL_UDP,
        transmit_buffer,
        segment_size
    );
}

bool udp_send_broadcast(
    u16 source_port,
    u16 destination_port,
    const void* payload,
    usize payload_size
)
{
    return ipv4_send_udp_broadcast(
        source_port,
        destination_port,
        payload,
        payload_size
    );
}

void udp_receive(
    u32 source_ip,
    u32 destination_ip,
    const u8* segment,
    usize segment_size
)
{
    if (segment == NULL || segment_size < sizeof(UdpHeader))
        return;

    const UdpHeader* header = (const UdpHeader*)segment;
    u16 source_port = net_read_be16((const u8*)&header->source_port);
    u16 destination_port = net_read_be16((const u8*)&header->destination_port);
    usize udp_length = net_read_be16((const u8*)&header->length);
    u16 received_checksum = net_read_be16((const u8*)&header->checksum);

    if (
        source_port == 0u ||
        destination_port == 0u ||
        udp_length < sizeof(UdpHeader) ||
        udp_length > segment_size
    )
    {
        return;
    }

    if (received_checksum != 0u)
    {
        u8 pseudo[12];
        net_write_be32(pseudo + 0u, source_ip);
        net_write_be32(pseudo + 4u, destination_ip);
        pseudo[8] = 0u;
        pseudo[9] = IPV4_PROTOCOL_UDP;
        net_write_be16(pseudo + 10u, (u16)udp_length);

        u32 sum = 0u;
        sum = checksum_add(pseudo, sizeof(pseudo), sum);
        sum = checksum_add(segment, udp_length, sum);
        while ((sum >> 16) != 0u)
            sum = (sum & 0xFFFFu) + (sum >> 16);

        if ((u16)sum != 0xFFFFu)
            return;
    }

    const u8* payload = segment + sizeof(UdpHeader);
    usize payload_size = udp_length - sizeof(UdpHeader);

    for (usize i = 0u; i < ROOT_UDP_MAX_BINDINGS; i++)
    {
        if (
            bindings[i].used &&
            bindings[i].port == destination_port &&
            bindings[i].handler != NULL
        )
        {
            bindings[i].handler(
                source_ip,
                source_port,
                destination_port,
                payload,
                payload_size,
                bindings[i].context
            );
            return;
        }
    }
}
