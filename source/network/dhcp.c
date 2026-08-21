#include "dhcp.h"

#include "net.h"
#include "ipv4.h"
#include "udp.h"
#include "time.h"
#include "memory.h"

#define DHCP_CLIENT_PORT 68u
#define DHCP_SERVER_PORT 67u
#define DHCP_MAGIC_COOKIE 0x63825363u

#define DHCP_MESSAGE_DISCOVER 1u
#define DHCP_MESSAGE_OFFER    2u
#define DHCP_MESSAGE_REQUEST  3u
#define DHCP_MESSAGE_ACK      5u
#define DHCP_MESSAGE_NAK      6u

#define DHCP_OPTION_SUBNET_MASK 1u
#define DHCP_OPTION_ROUTER      3u
#define DHCP_OPTION_DNS         6u
#define DHCP_OPTION_REQUESTED_IP 50u
#define DHCP_OPTION_LEASE_TIME  51u
#define DHCP_OPTION_MESSAGE_TYPE 53u
#define DHCP_OPTION_SERVER_ID   54u
#define DHCP_OPTION_PARAMETER_LIST 55u
#define DHCP_OPTION_CLIENT_ID   61u
#define DHCP_OPTION_END         255u
#define DHCP_OPTION_PAD         0u

#define DHCP_PACKET_SIZE 548u
#define DHCP_FIXED_SIZE 240u

static DhcpState state_value = DHCP_STATE_IDLE;
static const char* error_text = "none";
static u32 transaction_id = 0u;
static u32 offered_ip = 0u;
static u32 offered_mask = 0u;
static u32 offered_gateway = 0u;
static u32 offered_dns = 0u;
static u32 offered_server = 0u;
static u32 offered_lease = 0u;
static bool request_sent = false;
static u8 packet[DHCP_PACKET_SIZE];

static void reset_offer(void)
{
    offered_ip = 0u;
    offered_mask = 0u;
    offered_gateway = 0u;
    offered_dns = 0u;
    offered_server = 0u;
    offered_lease = 0u;
    request_sent = false;
}

static usize append_option(u8* output, usize offset, u8 code, const void* data, u8 size)
{
    if (offset + 2u + size >= DHCP_PACKET_SIZE)
        return offset;

    output[offset++] = code;
    output[offset++] = size;
    if (size > 0u && data != NULL)
        root_memcpy(output + offset, data, size);
    return offset + size;
}

static bool build_base(void)
{
    const RootNetConfig* config = net_config();
    if (!config->adapter_ready)
        return false;

    root_memzero(packet, sizeof(packet));
    packet[0] = 1u; /* BOOTREQUEST */
    packet[1] = 1u; /* Ethernet */
    packet[2] = 6u;
    packet[3] = 0u;
    net_write_be32(packet + 4u, transaction_id);
    net_write_be16(packet + 10u, 0x8000u); /* broadcast reply requested */
    root_memcpy(packet + 28u, config->mac, 6u);
    net_write_be32(packet + 236u, DHCP_MAGIC_COOKIE);
    return true;
}

static bool send_discover(void)
{
    if (!build_base())
        return false;

    usize offset = DHCP_FIXED_SIZE;
    u8 message_type = DHCP_MESSAGE_DISCOVER;
    offset = append_option(packet, offset, DHCP_OPTION_MESSAGE_TYPE, &message_type, 1u);

    u8 client_id[7];
    client_id[0] = 1u;
    root_memcpy(client_id + 1u, net_config()->mac, 6u);
    offset = append_option(packet, offset, DHCP_OPTION_CLIENT_ID, client_id, sizeof(client_id));

    const u8 params[] =
        { DHCP_OPTION_SUBNET_MASK, DHCP_OPTION_ROUTER, DHCP_OPTION_DNS,
          DHCP_OPTION_LEASE_TIME, DHCP_OPTION_SERVER_ID };
    offset = append_option(packet, offset, DHCP_OPTION_PARAMETER_LIST, params, sizeof(params));
    packet[offset++] = DHCP_OPTION_END;

    return ipv4_send_udp_broadcast(DHCP_CLIENT_PORT, DHCP_SERVER_PORT, packet, offset);
}

static bool send_request(void)
{
    if (offered_ip == 0u || offered_server == 0u || !build_base())
        return false;

    usize offset = DHCP_FIXED_SIZE;
    u8 message_type = DHCP_MESSAGE_REQUEST;
    offset = append_option(packet, offset, DHCP_OPTION_MESSAGE_TYPE, &message_type, 1u);

    u8 requested[4];
    net_write_be32(requested, offered_ip);
    offset = append_option(packet, offset, DHCP_OPTION_REQUESTED_IP, requested, 4u);

    u8 server[4];
    net_write_be32(server, offered_server);
    offset = append_option(packet, offset, DHCP_OPTION_SERVER_ID, server, 4u);

    u8 client_id[7];
    client_id[0] = 1u;
    root_memcpy(client_id + 1u, net_config()->mac, 6u);
    offset = append_option(packet, offset, DHCP_OPTION_CLIENT_ID, client_id, sizeof(client_id));

    const u8 params[] =
        { DHCP_OPTION_SUBNET_MASK, DHCP_OPTION_ROUTER, DHCP_OPTION_DNS,
          DHCP_OPTION_LEASE_TIME, DHCP_OPTION_SERVER_ID };
    offset = append_option(packet, offset, DHCP_OPTION_PARAMETER_LIST, params, sizeof(params));
    packet[offset++] = DHCP_OPTION_END;

    return ipv4_send_udp_broadcast(DHCP_CLIENT_PORT, DHCP_SERVER_PORT, packet, offset);
}

static void dhcp_udp_receive(
    u32 source_ip,
    u16 source_port,
    u16 destination_port,
    const u8* payload,
    usize payload_size,
    void* context
)
{
    (void)source_ip;
    (void)context;

    if (source_port != DHCP_SERVER_PORT || destination_port != DHCP_CLIENT_PORT)
        return;

    dhcp_receive(payload, payload_size);
}

void dhcp_init(void)
{
    state_value = DHCP_STATE_IDLE;
    error_text = "none";
    transaction_id = 0u;
    reset_offer();

    /* Port 68 remains permanently bound to the DHCP client. */
    (void)udp_bind(DHCP_CLIENT_PORT, dhcp_udp_receive, NULL);
}

static void parse_options(const u8* data, usize size, u8* message_type)
{
    usize offset = DHCP_FIXED_SIZE;
    *message_type = 0u;

    while (offset < size)
    {
        u8 code = data[offset++];
        if (code == DHCP_OPTION_END)
            break;
        if (code == DHCP_OPTION_PAD)
            continue;
        if (offset >= size)
            break;

        u8 length = data[offset++];
        if (offset + length > size)
            break;

        const u8* value = data + offset;

        if (code == DHCP_OPTION_MESSAGE_TYPE && length >= 1u)
            *message_type = value[0];
        else if (code == DHCP_OPTION_SUBNET_MASK && length >= 4u)
            offered_mask = net_read_be32(value);
        else if (code == DHCP_OPTION_ROUTER && length >= 4u)
            offered_gateway = net_read_be32(value);
        else if (code == DHCP_OPTION_DNS && length >= 4u)
            offered_dns = net_read_be32(value);
        else if (code == DHCP_OPTION_SERVER_ID && length >= 4u)
            offered_server = net_read_be32(value);
        else if (code == DHCP_OPTION_LEASE_TIME && length >= 4u)
            offered_lease = net_read_be32(value);

        offset += length;
    }
}

void dhcp_receive(const u8* payload, usize size)
{
    if (payload == NULL || size < DHCP_FIXED_SIZE)
        return;

    if (
        payload[0] != 2u ||
        payload[1] != 1u ||
        payload[2] != 6u ||
        net_read_be32(payload + 4u) != transaction_id ||
        net_read_be32(payload + 236u) != DHCP_MAGIC_COOKIE
    )
    {
        return;
    }

    const RootNetConfig* config = net_config();
    if (root_memcmp(payload + 28u, config->mac, 6u) != 0)
        return;

    u32 yiaddr = net_read_be32(payload + 16u);
    u8 message_type = 0u;
    parse_options(payload, size, &message_type);

    if (message_type == DHCP_MESSAGE_OFFER && state_value == DHCP_STATE_DISCOVERING)
    {
        offered_ip = yiaddr;
        state_value = DHCP_STATE_REQUESTING;
        return;
    }

    if (message_type == DHCP_MESSAGE_ACK && state_value == DHCP_STATE_REQUESTING)
    {
        if (yiaddr != 0u)
            offered_ip = yiaddr;

        net_apply_dhcp(
            offered_ip,
            offered_mask,
            offered_gateway,
            offered_dns,
            offered_server,
            offered_lease
        );

        state_value = DHCP_STATE_BOUND;
        error_text = "none";
        return;
    }

    if (message_type == DHCP_MESSAGE_NAK)
    {
        state_value = DHCP_STATE_FAILED;
        error_text = "server rejected request";
    }
}

bool dhcp_acquire(void)
{
    const RootNetConfig* config = net_config();

    if (!config->adapter_ready)
    {
        state_value = DHCP_STATE_FAILED;
        error_text = "no Ethernet adapter";
        return false;
    }

    if (!config->link_up)
    {
        state_value = DHCP_STATE_FAILED;
        error_text = "link is down";
        return false;
    }

    net_clear_ipv4();
    reset_offer();

    transaction_id =
        (u32)root_time_millis() ^
        ((u32)config->mac[2] << 24) ^
        ((u32)config->mac[3] << 16) ^
        ((u32)config->mac[4] << 8) ^
        (u32)config->mac[5] ^
        0x524F4F54u;

    if (transaction_id == 0u)
        transaction_id = 0x524F4F54u;

    for (u32 attempt = 0u; attempt < 3u; attempt++)
    {
        reset_offer();
        state_value = DHCP_STATE_DISCOVERING;
        error_text = "waiting for offer";

        if (!send_discover())
        {
            state_value = DHCP_STATE_FAILED;
            error_text = "could not transmit discover";
            return false;
        }

        u64 deadline = root_time_millis() + 3000u;

        while (root_time_millis() < deadline)
        {
            net_poll();

            if (state_value == DHCP_STATE_REQUESTING && !request_sent)
            {
                if (offered_ip == 0u || offered_server == 0u)
                {
                    state_value = DHCP_STATE_FAILED;
                    error_text = "invalid offer";
                    break;
                }

                if (!send_request())
                {
                    state_value = DHCP_STATE_FAILED;
                    error_text = "could not transmit request";
                    break;
                }

                request_sent = true;
                error_text = "waiting for acknowledgement";
                deadline = root_time_millis() + 3000u;
            }

            if (state_value == DHCP_STATE_BOUND)
                return true;

            if (state_value == DHCP_STATE_FAILED)
                break;

            __asm__ volatile ("hlt");
        }
    }

    state_value = DHCP_STATE_FAILED;
    error_text = "timeout waiting for DHCP";
    return false;
}

DhcpState dhcp_state(void)
{
    return state_value;
}

const char* dhcp_state_name(DhcpState state)
{
    switch (state)
    {
        case DHCP_STATE_IDLE: return "idle";
        case DHCP_STATE_DISCOVERING: return "discovering";
        case DHCP_STATE_REQUESTING: return "requesting";
        case DHCP_STATE_BOUND: return "bound";
        case DHCP_STATE_FAILED: return "failed";
        default: return "unknown";
    }
}

const char* dhcp_last_error(void)
{
    return error_text;
}
