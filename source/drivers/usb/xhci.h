#ifndef ROOTOS_XHCI_H
#define ROOTOS_XHCI_H

#include "types.h"
#include "pci.h"

#define XHCI_MAX_CONTROLLERS 4u

typedef struct
{
    u8 index;
    bool active;
    bool init_attempted;
    bool init_failed;
    u8 last_error;
    PciAddress pci_address;
    u32 mmio_base;
    u16 hci_version;
    u8 max_slots;
    u8 enabled_slots;
    u8 max_ports;
    u8 context_size;
    u16 scratchpad_count;
    usize enumerated_devices;
    u32 hotplug_events;
    u32 disconnect_events;
    u32 endpoint_recoveries;
    u32 transfer_errors;
} XhciControllerInfo;

typedef struct
{
    u8 controller_index;
    u8 port_number;
    u8 protocol_major;
    u8 protocol_minor;
    bool connected;
    bool enabled;
    bool powered;
    u8 speed_id;
    u8 link_state;
    u8 slot_id;
    bool has_usb_device;
    u32 usb_device_id;
} XhciPortInfo;

void xhci_init(void);
bool xhci_start(void);
bool xhci_rescan(void);
void xhci_poll(void);
bool xhci_any_running(void);

usize xhci_controller_count(void);
bool xhci_get_controller_info(
    usize index,
    XhciControllerInfo* output
);

usize xhci_port_count(void);
bool xhci_get_port_info(
    usize global_index,
    XhciPortInfo* output
);

const char* xhci_link_state_name(u8 state);
const char* xhci_protocol_name(u8 major);
const char* xhci_error_name(u8 error);

bool xhci_configure_bulk_endpoints(
    u32 usb_device_id,
    u8 configuration_value,
    u8 bulk_out_endpoint,
    u16 bulk_out_packet_size,
    u8 bulk_in_endpoint,
    u16 bulk_in_packet_size
);

bool xhci_configure_bulk_interface(
    u32 usb_device_id,
    u8 configuration_value,
    u8 interface_number,
    u8 alternate_setting,
    u8 bulk_out_endpoint,
    u16 bulk_out_packet_size,
    u8 bulk_in_endpoint,
    u16 bulk_in_packet_size
);

bool xhci_bulk_transfer(
    u32 usb_device_id,
    u8 endpoint_address,
    void* buffer,
    usize length
);

bool xhci_control_transfer(
    u32 usb_device_id,
    u8 request_type,
    u8 request,
    u16 value,
    u16 index,
    void* data,
    u16 length,
    usize* actual_length
);

bool xhci_bulk_transfer_ex(
    u32 usb_device_id,
    u8 endpoint_address,
    void* buffer,
    usize length,
    usize* actual_length
);

/* Non-blocking persistent Bulk-IN receive. The first call submits one IN TRB.
 * Later calls return completed=false until hardware finishes it. The caller's
 * buffer must remain valid while the request is pending. */
bool xhci_bulk_receive_poll(
    u32 usb_device_id,
    u8 endpoint_address,
    void* buffer,
    usize length,
    usize* actual_length,
    bool* completed
);

#endif
