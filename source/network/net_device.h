#ifndef ROOTOS_NET_DEVICE_H
#define ROOTOS_NET_DEVICE_H

#include "types.h"

#define ROOT_NET_DEVICE_MAX 8u
#define ROOT_NET_DRIVER_NAME_MAX 31u

typedef struct
{
    bool (*ready)(void* context);
    bool (*link_up)(void* context);
    bool (*send_frame)(void* context, const void* data, usize size);
    bool (*receive_frame)(void* context, void* output, usize capacity, usize* result_size);
} RootNetDeviceOps;

typedef struct
{
    bool used;
    u32 id;
    const char* driver_name;
    void* context;
    RootNetDeviceOps ops;
    u8 mac[6];
} RootNetDevice;

void net_device_init(void);
bool net_device_register(
    const char* driver_name,
    void* context,
    const RootNetDeviceOps* ops,
    const u8 mac[6]
);

usize net_device_count(void);
bool net_device_get(usize index, RootNetDevice* output);
const RootNetDevice* net_device_active(void);
bool net_device_select(usize index);
bool net_device_active_index(usize* output);
bool net_device_is_active(usize index);
const char* net_device_active_driver(void);
bool net_device_ready(void);
bool net_device_link_up(void);
bool net_device_send_frame(const void* data, usize size);
bool net_device_receive_frame(void* output, usize capacity, usize* result_size);
void net_device_get_mac(u8 output[6]);

#endif
