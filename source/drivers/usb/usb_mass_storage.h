#ifndef ROOTOS_USB_MASS_STORAGE_H
#define ROOTOS_USB_MASS_STORAGE_H

#include "types.h"

#define USB_MASS_MAX_DEVICES 8u

typedef struct
{
    bool used;
    u32 usb_device_id;
    u32 block_device_id;

    u8 interface_number;
    u8 bulk_in_endpoint;
    u8 bulk_out_endpoint;

    u32 block_size;
    u64 block_count;

    char vendor[9];
    char product[17];
    char revision[5];
} UsbMassStorageInfo;

void usb_mass_storage_init(void);
bool usb_mass_storage_scan(void);

usize usb_mass_storage_count(void);
bool usb_mass_storage_get(
    usize index,
    UsbMassStorageInfo* output
);

#endif
