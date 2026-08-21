#include "dns.h"

#include "udp.h"
#include "net.h"
#include "memory.h"
#include "string.h"
#include "time.h"

#define DNS_PORT 53u
#define DNS_PACKET_MAX 512u
#define DNS_TYPE_A 1u
#define DNS_CLASS_IN 1u
#define DNS_FLAG_QR 0x8000u
#define DNS_FLAG_TC 0x0200u
#define DNS_RCODE_MASK 0x000Fu

static u8 query_packet[DNS_PACKET_MAX];
static u8 response_packet[DNS_PACKET_MAX];
static usize response_size = 0u;
static bool response_ready = false;
static u16 active_id = 0u;
static u16 active_port = 0u;
static DnsCacheEntry cache[ROOT_DNS_CACHE_SIZE];
static usize cache_replace = 0u;

static bool dns_name_equal(const char* a, const char* b)
{
    if (a == NULL || b == NULL)
        return false;

    usize i = 0u;
    while (a[i] != '\0' && b[i] != '\0')
    {
        char ca = a[i];
        char cb = b[i];

        if (ca >= 'A' && ca <= 'Z')
            ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z')
            cb = (char)(cb - 'A' + 'a');

        if (ca != cb)
            return false;
        i++;
    }

    return a[i] == '\0' && b[i] == '\0';
}

static bool valid_name(const char* name)
{
    if (name == NULL || name[0] == '\0')
        return false;

    usize length = root_strlen(name);
    if (length == 0u || length > ROOT_DNS_NAME_MAX)
        return false;

    if (name[0] == '.' || name[length - 1u] == '.')
        return false;

    usize label = 0u;
    for (usize i = 0u; i < length; i++)
    {
        char c = name[i];
        if (c == '.')
        {
            if (label == 0u || label > 63u)
                return false;
            label = 0u;
            continue;
        }

        if (!(
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_'
        ))
        {
            return false;
        }

        label++;
        if (label > 63u)
            return false;
    }

    return label > 0u && label <= 63u;
}

static usize encode_name(const char* name, u8* output, usize capacity)
{
    usize input = 0u;
    usize output_index = 0u;

    while (name[input] != '\0')
    {
        usize label_start = input;
        usize label_size = 0u;

        while (name[input] != '\0' && name[input] != '.')
        {
            label_size++;
            input++;
        }

        if (
            label_size == 0u ||
            label_size > 63u ||
            output_index + 1u + label_size + 1u > capacity
        )
        {
            return 0u;
        }

        output[output_index++] = (u8)label_size;
        for (usize i = 0u; i < label_size; i++)
            output[output_index++] = (u8)name[label_start + i];

        if (name[input] == '.')
            input++;
    }

    if (output_index >= capacity)
        return 0u;

    output[output_index++] = 0u;
    return output_index;
}

static bool skip_name(const u8* packet, usize size, usize* offset)
{
    if (packet == NULL || offset == NULL || *offset >= size)
        return false;

    usize cursor = *offset;
    usize labels = 0u;

    while (cursor < size && labels < 128u)
    {
        u8 length = packet[cursor];

        if (length == 0u)
        {
            *offset = cursor + 1u;
            return true;
        }

        if ((length & 0xC0u) == 0xC0u)
        {
            if (cursor + 1u >= size)
                return false;
            *offset = cursor + 2u;
            return true;
        }

        if ((length & 0xC0u) != 0u || length > 63u)
            return false;

        cursor++;
        if (cursor + length > size)
            return false;

        cursor += length;
        labels++;
    }

    return false;
}

static void dns_receive(
    u32 source_ip,
    u16 source_port,
    u16 destination_port,
    const u8* payload,
    usize payload_size,
    void* context
)
{
    (void)context;

    if (
        source_port != DNS_PORT ||
        destination_port != active_port ||
        payload == NULL ||
        payload_size < 12u ||
        payload_size > sizeof(response_packet) ||
        source_ip != net_config()->dns_server ||
        net_read_be16(payload) != active_id
    )
    {
        return;
    }

    root_memcpy(response_packet, payload, payload_size);
    response_size = payload_size;
    response_ready = true;
}

static bool cache_lookup(const char* name, u32* address, u32* ttl)
{
    u32 now = (u32)root_time_millis();

    for (usize i = 0u; i < ROOT_DNS_CACHE_SIZE; i++)
    {
        if (!cache[i].used || !dns_name_equal(cache[i].name, name))
            continue;

        if ((i32)(cache[i].expires_ms - now) <= 0)
        {
            cache[i].used = false;
            continue;
        }

        if (address != NULL)
            *address = cache[i].address;
        if (ttl != NULL)
            *ttl = (cache[i].expires_ms - now) / 1000u;
        return true;
    }

    return false;
}

static void cache_store(const char* name, u32 address, u32 ttl_seconds)
{
    if (ttl_seconds == 0u)
        ttl_seconds = 1u;

    if (ttl_seconds > 86400u)
        ttl_seconds = 86400u;

    usize slot = ROOT_DNS_CACHE_SIZE;
    for (usize i = 0u; i < ROOT_DNS_CACHE_SIZE; i++)
    {
        if (cache[i].used && dns_name_equal(cache[i].name, name))
        {
            slot = i;
            break;
        }
        if (!cache[i].used && slot == ROOT_DNS_CACHE_SIZE)
            slot = i;
    }

    if (slot == ROOT_DNS_CACHE_SIZE)
    {
        slot = cache_replace;
        cache_replace = (cache_replace + 1u) % ROOT_DNS_CACHE_SIZE;
    }

    root_memzero(&cache[slot], sizeof(cache[slot]));
    cache[slot].used = true;
    (void)root_strlcpy(cache[slot].name, name, sizeof(cache[slot].name));
    cache[slot].address = address;
    cache[slot].expires_ms = (u32)root_time_millis() + ttl_seconds * 1000u;
}

void dns_init(void)
{
    root_memzero(cache, sizeof(cache));
    cache_replace = 0u;
    response_size = 0u;
    response_ready = false;
    active_id = 0u;
    active_port = 0u;
}

DnsResult dns_resolve_ipv4(const char* name, u32* address, u32* ttl_seconds)
{
    if (!valid_name(name) || address == NULL)
        return DNS_RESULT_INVALID_NAME;

    const RootNetConfig* config = net_config();
    if (!config->configured || config->dns_server == 0u)
        return DNS_RESULT_NOT_CONFIGURED;

    if (cache_lookup(name, address, ttl_seconds))
        return DNS_RESULT_OK;

    u16 port = udp_allocate_ephemeral_port();
    if (port == 0u)
        return DNS_RESULT_NO_PORT;

    response_ready = false;
    response_size = 0u;
    active_port = port;
    active_id = (u16)((u32)root_time_millis() ^ config->ipv4_address ^ port);
    if (active_id == 0u)
        active_id = 1u;

    if (!udp_bind(port, dns_receive, NULL))
    {
        active_port = 0u;
        return DNS_RESULT_NO_PORT;
    }

    root_memzero(query_packet, sizeof(query_packet));
    net_write_be16(query_packet + 0u, active_id);
    net_write_be16(query_packet + 2u, 0x0100u); /* recursion desired */
    net_write_be16(query_packet + 4u, 1u);

    usize offset = 12u;
    usize encoded = encode_name(name, query_packet + offset, sizeof(query_packet) - offset);
    if (encoded == 0u || offset + encoded + 4u > sizeof(query_packet))
    {
        udp_unbind(port);
        active_port = 0u;
        return DNS_RESULT_INVALID_NAME;
    }

    offset += encoded;
    net_write_be16(query_packet + offset, DNS_TYPE_A);
    offset += 2u;
    net_write_be16(query_packet + offset, DNS_CLASS_IN);
    offset += 2u;

    bool sent = false;
    for (u32 attempt = 0u; attempt < 2u && !response_ready; attempt++)
    {
        if (!udp_send_to(config->dns_server, port, DNS_PORT, query_packet, offset))
        {
            udp_unbind(port);
            active_port = 0u;
            return DNS_RESULT_SEND_FAILED;
        }
        sent = true;

        u64 deadline = root_time_millis() + 2500u;
        while (!response_ready && root_time_millis() < deadline)
        {
            net_poll();
            __asm__ volatile ("hlt");
        }
    }

    udp_unbind(port);
    active_port = 0u;

    if (!sent || !response_ready)
        return DNS_RESULT_TIMEOUT;

    if (response_size < 12u)
        return DNS_RESULT_MALFORMED;

    u16 flags = net_read_be16(response_packet + 2u);
    u16 questions = net_read_be16(response_packet + 4u);
    u16 answers = net_read_be16(response_packet + 6u);

    if ((flags & DNS_FLAG_QR) == 0u)
        return DNS_RESULT_MALFORMED;
    if ((flags & DNS_FLAG_TC) != 0u)
        return DNS_RESULT_TRUNCATED;
    if ((flags & DNS_RCODE_MASK) != 0u)
        return (flags & DNS_RCODE_MASK) == 3u ? DNS_RESULT_NOT_FOUND : DNS_RESULT_SERVER_ERROR;

    offset = 12u;
    for (u16 i = 0u; i < questions; i++)
    {
        if (!skip_name(response_packet, response_size, &offset) || offset + 4u > response_size)
            return DNS_RESULT_MALFORMED;
        offset += 4u;
    }

    for (u16 i = 0u; i < answers; i++)
    {
        if (!skip_name(response_packet, response_size, &offset) || offset + 10u > response_size)
            return DNS_RESULT_MALFORMED;

        u16 type = net_read_be16(response_packet + offset + 0u);
        u16 class_code = net_read_be16(response_packet + offset + 2u);
        u32 ttl = net_read_be32(response_packet + offset + 4u);
        u16 data_size = net_read_be16(response_packet + offset + 8u);
        offset += 10u;

        if (offset + data_size > response_size)
            return DNS_RESULT_MALFORMED;

        if (type == DNS_TYPE_A && class_code == DNS_CLASS_IN && data_size == 4u)
        {
            u32 result = net_read_be32(response_packet + offset);
            *address = result;
            if (ttl_seconds != NULL)
                *ttl_seconds = ttl;
            cache_store(name, result, ttl);
            return DNS_RESULT_OK;
        }

        offset += data_size;
    }

    return DNS_RESULT_NOT_FOUND;
}

const char* dns_result_name(DnsResult result)
{
    switch (result)
    {
        case DNS_RESULT_OK: return "ok";
        case DNS_RESULT_INVALID_NAME: return "invalid name";
        case DNS_RESULT_NOT_CONFIGURED: return "network/DNS not configured";
        case DNS_RESULT_NO_PORT: return "no UDP port available";
        case DNS_RESULT_SEND_FAILED: return "send failed";
        case DNS_RESULT_TIMEOUT: return "timeout";
        case DNS_RESULT_SERVER_ERROR: return "DNS server error";
        case DNS_RESULT_NOT_FOUND: return "not found";
        case DNS_RESULT_TRUNCATED: return "truncated response (TCP DNS not implemented yet)";
        case DNS_RESULT_MALFORMED: return "malformed response";
        default: return "unknown";
    }
}

void dns_cache_flush(void)
{
    root_memzero(cache, sizeof(cache));
    cache_replace = 0u;
}

usize dns_cache_count(void)
{
    usize count = 0u;
    u32 now = (u32)root_time_millis();
    for (usize i = 0u; i < ROOT_DNS_CACHE_SIZE; i++)
    {
        if (cache[i].used && (i32)(cache[i].expires_ms - now) > 0)
            count++;
    }
    return count;
}

bool dns_cache_get(usize index, DnsCacheEntry* output)
{
    if (output == NULL)
        return false;

    usize logical = 0u;
    u32 now = (u32)root_time_millis();
    for (usize i = 0u; i < ROOT_DNS_CACHE_SIZE; i++)
    {
        if (!cache[i].used || (i32)(cache[i].expires_ms - now) <= 0)
            continue;

        if (logical == index)
        {
            *output = cache[i];
            return true;
        }
        logical++;
    }
    return false;
}
