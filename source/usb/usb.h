#ifndef ROOTOS_USB_H
#define ROOTOS_USB_H

#include "types.h"

#define USB_MAX_DEVICES 32u
#define USB_MAX_INTERFACES 8u
#define USB_MAX_ENDPOINTS 8u
#define USB_STRING_MAX 96u
#define USB_MAX_LISTENERS 8u

typedef enum
{
    USB_SPEED_UNKNOWN = 0,
    USB_SPEED_LOW,
    USB_SPEED_FULL,
    USB_SPEED_HIGH,
    USB_SPEED_SUPER,
    USB_SPEED_SUPER_PLUS
} UsbSpeed;

typedef struct
{
    u8 address;
    u8 attributes;
    u16 max_packet_size;
    u8 interval;

    /* USB 3.x SuperSpeed Endpoint Companion descriptor.  Zero for USB 2.x. */
    u8 max_burst;
    u8 companion_attributes;
    u16 bytes_per_interval;
} UsbEndpointInfo;

typedef struct
{
    u8 number;
    u8 alternate_setting;
    u8 declared_endpoint_count;
    u8 class_code;
    u8 subclass;
    u8 protocol;
    u8 string_index;
    u8 endpoint_count;
    UsbEndpointInfo endpoints[USB_MAX_ENDPOINTS];
} UsbInterfaceInfo;

typedef struct
{
    u32 id;

    u8 controller_index;
    u8 port_number;
    u8 slot_id;
    u8 address;

    UsbSpeed speed;
    u8 speed_id;

    u16 vendor_id;
    u16 product_id;
    u16 usb_version_bcd;
    u16 device_version_bcd;

    u8 device_class;
    u8 device_subclass;
    u8 device_protocol;
    u16 endpoint0_max_packet;

    u8 configuration_count;
    u8 configuration_value;
    u8 configuration_attributes;
    u16 max_power_ma;

    char manufacturer[USB_STRING_MAX];
    char product[USB_STRING_MAX];
    char serial[USB_STRING_MAX];

    u8 interface_count;
    UsbInterfaceInfo interfaces[USB_MAX_INTERFACES];
} UsbDeviceInfo;

typedef void (*UsbDeviceAddedCallback)(const UsbDeviceInfo* device);
typedef void (*UsbDeviceRemovedCallback)(u32 usb_device_id);

void usb_init(void);
void usb_reset_inventory(void);

/* Stable device IDs: IDs are slots and are not renumbered when another USB
 * device is unplugged. usb_get_device(index) remains an ordinal iterator for
 * existing callers; usb_get_device_by_id() is used when an ID came from UI. */
bool usb_register_device(
    const UsbDeviceInfo* device,
    u32* assigned_id
);

bool usb_remove_device(u32 usb_device_id);

usize usb_device_count(void);
bool usb_get_device(usize index, UsbDeviceInfo* output);
bool usb_get_device_by_id(u32 usb_device_id, UsbDeviceInfo* output);
bool usb_device_present(u32 usb_device_id);

/* Lifecycle notifications are deferred for additions so class drivers are not
 * called recursively while the HCD is still finishing enumeration. Removals
 * are delivered synchronously because drivers must immediately stop I/O. */
bool usb_register_listener(
    UsbDeviceAddedCallback added,
    UsbDeviceRemovedCallback removed
);
void usb_service(void);

const char* usb_speed_name(UsbSpeed speed);
const char* usb_class_name(u8 class_code);
const char* usb_transfer_type_name(u8 endpoint_attributes);
const char* usb_direction_name(u8 endpoint_address);

#endif
