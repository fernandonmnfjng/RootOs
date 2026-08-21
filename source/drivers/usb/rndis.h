#ifndef ROOTOS_RNDIS_H
#define ROOTOS_RNDIS_H

#include "types.h"

#define RNDIS_MAX_DEVICES 4u

typedef struct
{
    bool used;
    bool ready;
    bool link_up;
    u32 usb_device_id;
    u16 vendor_id;
    u16 product_id;
    u8 control_interface;
    u8 data_interface;
    u8 data_alternate_setting;
    u8 bulk_in_endpoint;
    u8 bulk_out_endpoint;
    u16 bulk_in_packet_size;
    u16 bulk_out_packet_size;
    u8 mac[6];
    const char* last_error;
} RndisDeviceInfo;

void rndis_init(void);
void rndis_service(void);
void rndis_invalidate_all(void);
bool rndis_probe_all(void);
usize rndis_device_count(void);
bool rndis_get_device(usize index, RndisDeviceInfo* output);
const char* rndis_last_error(void);

#endif
