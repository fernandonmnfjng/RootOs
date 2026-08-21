#include "usb.h"
#include "memory.h"

typedef struct
{
    UsbDeviceAddedCallback added;
    UsbDeviceRemovedCallback removed;
} UsbListener;

static UsbDeviceInfo usb_devices[USB_MAX_DEVICES];
static bool usb_used[USB_MAX_DEVICES];
static bool usb_pending_add[USB_MAX_DEVICES];
static usize usb_count = 0u;
static UsbListener listeners[USB_MAX_LISTENERS];
static usize listener_count = 0u;

void usb_init(void)
{
    root_memzero(listeners, sizeof(listeners));
    listener_count = 0u;
    usb_reset_inventory();
}

void usb_reset_inventory(void)
{
    /* Notify class drivers before destroying identity/state. */
    for (u32 id = 0u; id < USB_MAX_DEVICES; id++)
    {
        if (!usb_used[id])
            continue;

        for (usize listener = 0u; listener < listener_count; listener++)
        {
            if (listeners[listener].removed != NULL)
                listeners[listener].removed(id);
        }
    }

    root_memzero(usb_devices, sizeof(usb_devices));
    root_memzero(usb_used, sizeof(usb_used));
    root_memzero(usb_pending_add, sizeof(usb_pending_add));
    usb_count = 0u;
}

bool usb_register_device(
    const UsbDeviceInfo* device,
    u32* assigned_id
)
{
    if (device == NULL || usb_count >= USB_MAX_DEVICES)
        return false;

    u32 slot = USB_MAX_DEVICES;
    for (u32 i = 0u; i < USB_MAX_DEVICES; i++)
    {
        if (!usb_used[i])
        {
            slot = i;
            break;
        }
    }

    if (slot >= USB_MAX_DEVICES)
        return false;

    root_memcpy(&usb_devices[slot], device, sizeof(*device));
    usb_devices[slot].id = slot;
    usb_used[slot] = true;
    usb_pending_add[slot] = true;
    usb_count++;

    if (assigned_id != NULL)
        *assigned_id = slot;

    return true;
}

bool usb_remove_device(u32 usb_device_id)
{
    if (usb_device_id >= USB_MAX_DEVICES || !usb_used[usb_device_id])
        return false;

    /* First stop all class-driver activity while descriptors are still valid. */
    for (usize listener = 0u; listener < listener_count; listener++)
    {
        if (listeners[listener].removed != NULL)
            listeners[listener].removed(usb_device_id);
    }

    root_memzero(&usb_devices[usb_device_id], sizeof(usb_devices[usb_device_id]));
    usb_used[usb_device_id] = false;
    usb_pending_add[usb_device_id] = false;
    if (usb_count > 0u)
        usb_count--;

    return true;
}

usize usb_device_count(void)
{
    return usb_count;
}

bool usb_get_device(usize index, UsbDeviceInfo* output)
{
    if (output == NULL || index >= usb_count)
        return false;

    usize visible = 0u;
    for (u32 id = 0u; id < USB_MAX_DEVICES; id++)
    {
        if (!usb_used[id])
            continue;

        if (visible++ != index)
            continue;

        root_memcpy(output, &usb_devices[id], sizeof(*output));
        return true;
    }

    return false;
}

bool usb_get_device_by_id(u32 usb_device_id, UsbDeviceInfo* output)
{
    if (
        output == NULL ||
        usb_device_id >= USB_MAX_DEVICES ||
        !usb_used[usb_device_id]
    )
    {
        return false;
    }

    root_memcpy(output, &usb_devices[usb_device_id], sizeof(*output));
    return true;
}

bool usb_device_present(u32 usb_device_id)
{
    return usb_device_id < USB_MAX_DEVICES && usb_used[usb_device_id];
}

bool usb_register_listener(
    UsbDeviceAddedCallback added,
    UsbDeviceRemovedCallback removed
)
{
    if (
        (added == NULL && removed == NULL) ||
        listener_count >= USB_MAX_LISTENERS
    )
    {
        return false;
    }

    listeners[listener_count].added = added;
    listeners[listener_count].removed = removed;
    listener_count++;
    return true;
}

void usb_service(void)
{
    /* Deliver all newly enumerated devices after the HCD has returned to its
     * top-level polling loop. This keeps class-driver control/bulk requests
     * from recursively entering xHCI enumeration. */
    for (u32 id = 0u; id < USB_MAX_DEVICES; id++)
    {
        if (!usb_used[id] || !usb_pending_add[id])
            continue;

        usb_pending_add[id] = false;

        for (usize listener = 0u; listener < listener_count; listener++)
        {
            if (listeners[listener].added != NULL)
                listeners[listener].added(&usb_devices[id]);
        }
    }
}

const char* usb_speed_name(UsbSpeed speed)
{
    switch (speed)
    {
        case USB_SPEED_LOW: return "Low-speed (1.5 Mb/s)";
        case USB_SPEED_FULL: return "Full-speed (12 Mb/s)";
        case USB_SPEED_HIGH: return "High-speed (480 Mb/s)";
        case USB_SPEED_SUPER: return "SuperSpeed (5 Gb/s)";
        case USB_SPEED_SUPER_PLUS: return "SuperSpeedPlus";
        default: return "Unknown";
    }
}

const char* usb_class_name(u8 class_code)
{
    switch (class_code)
    {
        case 0x00u: return "Per-interface";
        case 0x01u: return "Audio";
        case 0x02u: return "Communications";
        case 0x03u: return "HID";
        case 0x05u: return "Physical";
        case 0x06u: return "Imaging";
        case 0x07u: return "Printer";
        case 0x08u: return "Mass storage";
        case 0x09u: return "Hub";
        case 0x0Au: return "CDC data";
        case 0x0Bu: return "Smart card";
        case 0x0Du: return "Content security";
        case 0x0Eu: return "Video";
        case 0x0Fu: return "Personal healthcare";
        case 0x10u: return "Audio/Video";
        case 0x11u: return "Billboard";
        case 0x12u: return "USB Type-C bridge";
        case 0xDCu: return "Diagnostic";
        case 0xE0u: return "Wireless";
        case 0xEFu: return "Miscellaneous";
        case 0xFEu: return "Application specific";
        case 0xFFu: return "Vendor specific";
        default: return "Unknown";
    }
}

const char* usb_transfer_type_name(u8 endpoint_attributes)
{
    switch (endpoint_attributes & 0x03u)
    {
        case 0u: return "Control";
        case 1u: return "Isochronous";
        case 2u: return "Bulk";
        case 3u: return "Interrupt";
        default: return "Unknown";
    }
}

const char* usb_direction_name(u8 endpoint_address)
{
    return (endpoint_address & 0x80u) != 0u ? "IN" : "OUT";
}
