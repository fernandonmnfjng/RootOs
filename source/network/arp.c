#include "arp.h"

#include "net.h"
#include "memory.h"

#define ARP_OPERATION_REQUEST 1u
#define ARP_OPERATION_REPLY   2u

#pragma pack(push, 1)
typedef struct
{
    u8 destination[6];
    u8 source[6];
    u16 ether_type;

    u16 hardware_type;
    u16 protocol_type;
    u8 hardware_length;
    u8 protocol_length;
    u16 operation;
    u8 sender_mac[6];
    u8 sender_ip[4];
    u8 target_mac[6];
    u8 target_ip[4];
} ArpFrame;
#pragma pack(pop)

static ArpEntry entries[ARP_TABLE_SIZE];
static usize replacement_index = 0u;

static void arp_remember(u32 ipv4, const u8 mac[6])
{
    if (ipv4 == 0u || mac == NULL)
        return;

    for (usize i = 0u; i < ARP_TABLE_SIZE; i++)
    {
        if (entries[i].used && entries[i].ipv4 == ipv4)
        {
            root_memcpy(entries[i].mac, mac, 6u);
            return;
        }
    }

    for (usize i = 0u; i < ARP_TABLE_SIZE; i++)
    {
        if (!entries[i].used)
        {
            entries[i].used = true;
            entries[i].ipv4 = ipv4;
            root_memcpy(entries[i].mac, mac, 6u);
            return;
        }
    }

    entries[replacement_index].used = true;
    entries[replacement_index].ipv4 = ipv4;
    root_memcpy(entries[replacement_index].mac, mac, 6u);
    replacement_index = (replacement_index + 1u) % ARP_TABLE_SIZE;
}

void arp_init(void)
{
    root_memzero(entries, sizeof(entries));
    replacement_index = 0u;
}

bool arp_lookup(u32 ipv4, u8 mac[6])
{
    if (mac == NULL)
        return false;

    for (usize i = 0u; i < ARP_TABLE_SIZE; i++)
    {
        if (entries[i].used && entries[i].ipv4 == ipv4)
        {
            root_memcpy(mac, entries[i].mac, 6u);
            return true;
        }
    }

    return false;
}

bool arp_request(u32 ipv4)
{
    const RootNetConfig* config = net_config();
    if (!config->configured || ipv4 == 0u)
        return false;

    u8 payload[28];
    root_memzero(payload, sizeof(payload));

    net_write_be16(payload + 0u, 1u);
    net_write_be16(payload + 2u, ROOT_ETH_TYPE_IPV4);
    payload[4] = 6u;
    payload[5] = 4u;
    net_write_be16(payload + 6u, ARP_OPERATION_REQUEST);
    root_memcpy(payload + 8u, config->mac, 6u);
    net_write_be32(payload + 14u, config->ipv4_address);
    root_memzero(payload + 18u, 6u);
    net_write_be32(payload + 24u, ipv4);

    static const u8 broadcast[6] =
        { 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu };

    return net_send_ethernet(broadcast, ROOT_ETH_TYPE_ARP, payload, sizeof(payload));
}

void arp_receive(const u8* frame, usize size)
{
    if (frame == NULL || size < sizeof(ArpFrame))
        return;

    const ArpFrame* packet = (const ArpFrame*)frame;

    if (
        net_read_be16((const u8*)&packet->hardware_type) != 1u ||
        net_read_be16((const u8*)&packet->protocol_type) != ROOT_ETH_TYPE_IPV4 ||
        packet->hardware_length != 6u ||
        packet->protocol_length != 4u
    )
    {
        return;
    }

    u16 operation = net_read_be16((const u8*)&packet->operation);
    u32 sender_ip = net_read_be32(packet->sender_ip);
    u32 target_ip = net_read_be32(packet->target_ip);

    arp_remember(sender_ip, packet->sender_mac);

    const RootNetConfig* config = net_config();
    if (
        operation != ARP_OPERATION_REQUEST ||
        !config->configured ||
        target_ip != config->ipv4_address
    )
    {
        return;
    }

    u8 payload[28];
    root_memzero(payload, sizeof(payload));
    net_write_be16(payload + 0u, 1u);
    net_write_be16(payload + 2u, ROOT_ETH_TYPE_IPV4);
    payload[4] = 6u;
    payload[5] = 4u;
    net_write_be16(payload + 6u, ARP_OPERATION_REPLY);
    root_memcpy(payload + 8u, config->mac, 6u);
    net_write_be32(payload + 14u, config->ipv4_address);
    root_memcpy(payload + 18u, packet->sender_mac, 6u);
    net_write_be32(payload + 24u, sender_ip);

    (void)net_send_ethernet(packet->sender_mac, ROOT_ETH_TYPE_ARP, payload, sizeof(payload));
}

usize arp_entry_count(void)
{
    usize count = 0u;
    for (usize i = 0u; i < ARP_TABLE_SIZE; i++)
        if (entries[i].used)
            count++;
    return count;
}

bool arp_get_entry(usize index, ArpEntry* output)
{
    if (output == NULL)
        return false;

    usize logical = 0u;
    for (usize i = 0u; i < ARP_TABLE_SIZE; i++)
    {
        if (!entries[i].used)
            continue;

        if (logical == index)
        {
            *output = entries[i];
            return true;
        }

        logical++;
    }

    return false;
}
