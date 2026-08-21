#ifndef ROOTOS_DNS_H
#define ROOTOS_DNS_H

#include "types.h"

#define ROOT_DNS_NAME_MAX 253u
#define ROOT_DNS_CACHE_SIZE 16u

typedef enum
{
    DNS_RESULT_OK = 0,
    DNS_RESULT_INVALID_NAME,
    DNS_RESULT_NOT_CONFIGURED,
    DNS_RESULT_NO_PORT,
    DNS_RESULT_SEND_FAILED,
    DNS_RESULT_TIMEOUT,
    DNS_RESULT_SERVER_ERROR,
    DNS_RESULT_NOT_FOUND,
    DNS_RESULT_TRUNCATED,
    DNS_RESULT_MALFORMED
} DnsResult;

typedef struct
{
    bool used;
    char name[ROOT_DNS_NAME_MAX + 1u];
    u32 address;
    u32 expires_ms;
} DnsCacheEntry;

void dns_init(void);
DnsResult dns_resolve_ipv4(const char* name, u32* address, u32* ttl_seconds);
const char* dns_result_name(DnsResult result);
void dns_cache_flush(void);
usize dns_cache_count(void);
bool dns_cache_get(usize index, DnsCacheEntry* output);

#endif
