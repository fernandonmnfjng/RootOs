#include "tcp.h"

#include "ipv4.h"
#include "net.h"
#include "memory.h"
#include "time.h"

#define IPV4_PROTOCOL_TCP 6u

#define TCP_FLAG_FIN 0x01u
#define TCP_FLAG_SYN 0x02u
#define TCP_FLAG_RST 0x04u
#define TCP_FLAG_PSH 0x08u
#define TCP_FLAG_ACK 0x10u

#define TCP_WINDOW 32768u
#define TCP_DEFAULT_TIMEOUT 5000u
#define TCP_HEADER_BYTES 20u
#define TCP_SYN_HEADER_BYTES 24u

#pragma pack(push, 1)
typedef struct
{
    u16 source_port;
    u16 destination_port;
    u32 sequence;
    u32 acknowledgment;
    u8 data_offset_reserved;
    u8 flags;
    u16 window;
    u16 checksum;
    u16 urgent_pointer;
} TcpHeader;
#pragma pack(pop)

static RootTcpConnection connections[ROOT_TCP_MAX_CONNECTIONS];
static u8 transmit_buffer[ROOT_ETH_MTU];
static u16 next_port = 50000u;

static u32 checksum_add(const u8* data, usize size, u32 sum)
{
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

static u16 tcp_checksum(u32 source_ip, u32 destination_ip, const u8* segment, usize size)
{
    u8 pseudo[12];
    net_write_be32(pseudo + 0u, source_ip);
    net_write_be32(pseudo + 4u, destination_ip);
    pseudo[8] = 0u;
    pseudo[9] = IPV4_PROTOCOL_TCP;
    net_write_be16(pseudo + 10u, (u16)size);

    u32 sum = 0u;
    sum = checksum_add(pseudo, sizeof(pseudo), sum);
    sum = checksum_add(segment, size, sum);
    while ((sum >> 16) != 0u)
        sum = (sum & 0xFFFFu) + (sum >> 16);
    return (u16)(~sum & 0xFFFFu);
}

static bool local_port_used(u16 port)
{
    for (usize i = 0u; i < ROOT_TCP_MAX_CONNECTIONS; i++)
        if (connections[i].used && connections[i].local_port == port)
            return true;
    return false;
}

static u16 allocate_port(void)
{
    for (u32 tries = 0u; tries < 10000u; tries++)
    {
        u16 port = next_port++;
        if (next_port < 50000u || next_port > 60000u)
            next_port = 50000u;
        if (!local_port_used(port))
            return port;
    }
    return 0u;
}

static bool send_segment(
    RootTcpConnection* connection,
    u8 flags,
    u32 sequence,
    u32 acknowledgment,
    bool include_mss
)
{
    if (connection == NULL || !net_config()->configured)
        return false;

    usize header_size = include_mss ? TCP_SYN_HEADER_BYTES : TCP_HEADER_BYTES;
    root_memzero(transmit_buffer, header_size);

    TcpHeader* header = (TcpHeader*)transmit_buffer;
    net_write_be16((u8*)&header->source_port, connection->local_port);
    net_write_be16((u8*)&header->destination_port, connection->remote_port);
    net_write_be32((u8*)&header->sequence, sequence);
    net_write_be32((u8*)&header->acknowledgment, acknowledgment);
    header->data_offset_reserved = (u8)((header_size / 4u) << 4);
    header->flags = flags;
    net_write_be16((u8*)&header->window, TCP_WINDOW);
    net_write_be16((u8*)&header->checksum, 0u);
    net_write_be16((u8*)&header->urgent_pointer, 0u);

    if (include_mss)
    {
        transmit_buffer[20] = 2u; /* MSS */
        transmit_buffer[21] = 4u;
        net_write_be16(transmit_buffer + 22u, 1460u);
    }

    u16 checksum = tcp_checksum(
        net_config()->ipv4_address,
        connection->remote_ip,
        transmit_buffer,
        header_size
    );
    net_write_be16((u8*)&header->checksum, checksum);

    return ipv4_send_packet(
        connection->remote_ip,
        IPV4_PROTOCOL_TCP,
        transmit_buffer,
        header_size
    );
}

void tcp_init(void)
{
    root_memzero(connections, sizeof(connections));
    next_port = 50000u;
}

int tcp_connect(u32 remote_ip, u16 remote_port, u32 timeout_ms)
{
    if (!net_config()->configured || remote_ip == 0u || remote_port == 0u)
        return -1;

    int slot = -1;
    for (usize i = 0u; i < ROOT_TCP_MAX_CONNECTIONS; i++)
    {
        if (!connections[i].used)
        {
            slot = (int)i;
            break;
        }
    }

    if (slot < 0)
        return -1;

    u16 local_port = allocate_port();
    if (local_port == 0u)
        return -1;

    RootTcpConnection* connection = &connections[slot];
    root_memzero(connection, sizeof(*connection));
    connection->used = true;
    connection->remote_ip = remote_ip;
    connection->remote_port = remote_port;
    connection->local_port = local_port;
    connection->state = ROOT_TCP_SYN_SENT;
    connection->last_error = "none";

    u32 initial_sequence =
        (u32)root_time_millis() ^
        remote_ip ^
        ((u32)remote_port << 16) ^
        (u32)local_port;
    if (initial_sequence == 0u)
        initial_sequence = 1u;

    connection->send_next = initial_sequence + 1u;

    if (timeout_ms == 0u)
        timeout_ms = TCP_DEFAULT_TIMEOUT;

    u32 per_attempt = timeout_ms / 3u;
    if (per_attempt < 1000u)
        per_attempt = 1000u;

    bool transmitted = false;
    for (u32 attempt = 0u; attempt < 3u && connection->state == ROOT_TCP_SYN_SENT; attempt++)
    {
        if (!send_segment(connection, TCP_FLAG_SYN, initial_sequence, 0u, true))
        {
            if (!transmitted)
            {
                connection->state = ROOT_TCP_ERROR;
                connection->last_error = "SYN send failed";
                return slot;
            }
            break;
        }

        transmitted = true;
        u64 deadline = root_time_millis() + per_attempt;
        while (
            connection->state == ROOT_TCP_SYN_SENT &&
            root_time_millis() < deadline
        )
        {
            net_poll();
            __asm__ volatile ("hlt");
        }
    }

    if (connection->state == ROOT_TCP_SYN_SENT)
    {
        connection->state = ROOT_TCP_ERROR;
        connection->last_error = "connect timeout";
    }

    return slot;
}

bool tcp_close(int id)
{
    if (id < 0 || id >= (int)ROOT_TCP_MAX_CONNECTIONS || !connections[id].used)
        return false;

    RootTcpConnection* connection = &connections[id];

    if (connection->state == ROOT_TCP_ESTABLISHED)
    {
        /* v0.47 does not implement the complete close state machine yet.
         * Send RST so tests do not leave a half-open connection behind. */
        (void)send_segment(
            connection,
            TCP_FLAG_RST | TCP_FLAG_ACK,
            connection->send_next,
            connection->receive_next,
            false
        );
    }

    root_memzero(connection, sizeof(*connection));
    return true;
}

void tcp_receive(
    u32 source_ip,
    u32 destination_ip,
    const u8* segment,
    usize segment_size
)
{
    if (
        segment == NULL ||
        segment_size < TCP_HEADER_BYTES ||
        destination_ip != net_config()->ipv4_address
    )
    {
        return;
    }

    const TcpHeader* header = (const TcpHeader*)segment;
    u16 source_port = net_read_be16((const u8*)&header->source_port);
    u16 destination_port = net_read_be16((const u8*)&header->destination_port);
    usize header_size = (usize)(header->data_offset_reserved >> 4) * 4u;

    if (header_size < TCP_HEADER_BYTES || header_size > segment_size)
        return;

    if (tcp_checksum(source_ip, destination_ip, segment, segment_size) != 0u)
        return;

    for (usize i = 0u; i < ROOT_TCP_MAX_CONNECTIONS; i++)
    {
        RootTcpConnection* connection = &connections[i];
        if (
            !connection->used ||
            connection->remote_ip != source_ip ||
            connection->remote_port != source_port ||
            connection->local_port != destination_port
        )
        {
            continue;
        }

        u32 sequence = net_read_be32((const u8*)&header->sequence);
        u32 acknowledgment = net_read_be32((const u8*)&header->acknowledgment);
        u8 flags = header->flags;

        if ((flags & TCP_FLAG_RST) != 0u)
        {
            connection->state = ROOT_TCP_RESET;
            connection->last_error = "peer reset";
            return;
        }

        if (connection->state == ROOT_TCP_SYN_SENT)
        {
            if (
                (flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK) &&
                acknowledgment == connection->send_next
            )
            {
                connection->receive_next = sequence + 1u;

                if (!send_segment(
                    connection,
                    TCP_FLAG_ACK,
                    connection->send_next,
                    connection->receive_next,
                    false
                ))
                {
                    connection->state = ROOT_TCP_ERROR;
                    connection->last_error = "final ACK send failed";
                    return;
                }

                connection->state = ROOT_TCP_ESTABLISHED;
                connection->last_error = "none";
            }
            return;
        }

        if (connection->state == ROOT_TCP_ESTABLISHED)
        {
            usize data_size = segment_size - header_size;
            if (sequence == connection->receive_next && data_size > 0u)
            {
                connection->receive_next += (u32)data_size;
                (void)send_segment(
                    connection,
                    TCP_FLAG_ACK,
                    connection->send_next,
                    connection->receive_next,
                    false
                );
            }

            if ((flags & TCP_FLAG_FIN) != 0u)
            {
                connection->receive_next++;
                (void)send_segment(
                    connection,
                    TCP_FLAG_ACK,
                    connection->send_next,
                    connection->receive_next,
                    false
                );
                connection->state = ROOT_TCP_FIN_WAIT;
            }
        }

        return;
    }
}

RootTcpState tcp_state(int id)
{
    if (id < 0 || id >= (int)ROOT_TCP_MAX_CONNECTIONS || !connections[id].used)
        return ROOT_TCP_CLOSED;
    return connections[id].state;
}

const char* tcp_state_name(RootTcpState state)
{
    switch (state)
    {
        case ROOT_TCP_CLOSED: return "CLOSED";
        case ROOT_TCP_SYN_SENT: return "SYN-SENT";
        case ROOT_TCP_ESTABLISHED: return "ESTABLISHED";
        case ROOT_TCP_FIN_WAIT: return "FIN-WAIT";
        case ROOT_TCP_RESET: return "RESET";
        case ROOT_TCP_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

const char* tcp_last_error(int id)
{
    if (id < 0 || id >= (int)ROOT_TCP_MAX_CONNECTIONS || !connections[id].used)
        return "invalid connection";
    return connections[id].last_error != NULL ? connections[id].last_error : "none";
}

usize tcp_connection_count(void)
{
    usize count = 0u;
    for (usize i = 0u; i < ROOT_TCP_MAX_CONNECTIONS; i++)
        if (connections[i].used)
            count++;
    return count;
}

bool tcp_get_connection(usize index, RootTcpConnection* output, int* id)
{
    if (output == NULL)
        return false;

    usize logical = 0u;
    for (usize i = 0u; i < ROOT_TCP_MAX_CONNECTIONS; i++)
    {
        if (!connections[i].used)
            continue;

        if (logical == index)
        {
            *output = connections[i];
            if (id != NULL)
                *id = (int)i;
            return true;
        }
        logical++;
    }
    return false;
}
