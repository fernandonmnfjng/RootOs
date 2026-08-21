#ifndef ROOTOS_ARP_H
#define ROOTOS_ARP_H

#include "types.h"

#define ARP_TABLE_SIZE 16u

typedef struct
{
    bool used;
    u32 ipv4;
    u8 mac[6];
} ArpEntry;

void arp_init(void);
void arp_receive(const u8* frame, usize size);
bool arp_request(u32 ipv4);

usize arp_entry_count(void);
bool arp_get_entry(usize index, ArpEntry* output);
bool arp_lookup(u32 ipv4, u8 mac[6]);

#endif
