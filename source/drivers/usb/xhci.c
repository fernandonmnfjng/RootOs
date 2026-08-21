#include "xhci.h"

#include "device_manager.h"
#include "usb.h"
#include "memory.h"
#include "time.h"

/*
 * RootOS v0.44 xHCI driver
 *
 * This first xHCI stage is deliberately polling based.  It initializes the
 * controller, enumerates devices directly attached to root ports, addresses
 * them and reads standard USB descriptors.  Interrupt driven transfers, hub
 * traversal and HID input are separate layers and can be added without
 * changing the public USB inventory API.
 */

#define XHCI_MAX_TRACKED_DEVICES 32u
#define XHCI_MAX_SCRATCHPADS 32u
#define XHCI_DCBAA_ENTRIES 264u

#define XHCI_COMMAND_TRBS 128u
#define XHCI_EVENT_TRBS 256u
#define XHCI_TRANSFER_TRBS 128u
#define XHCI_CONTROL_BUFFER_SIZE 2048u

#define XHCI_DEVICE_CONTEXT_BYTES 2048u
#define XHCI_INPUT_CONTEXT_BYTES 2112u

/*
 * Bounded waits: a broken USB controller must never freeze the whole OS.
 * PIT interrupts are already enabled before xHCI is started, so HLT can be
 * used as a cheap yield between short polling bursts.
 */
#define XHCI_WAIT_ROUNDS 64u
#define XHCI_WAIT_PAUSES_PER_ROUND 256u

#define XHCI_CAP_CAPLENGTH 0x00u
#define XHCI_CAP_HCIVERSION 0x02u
#define XHCI_CAP_HCSPARAMS1 0x04u
#define XHCI_CAP_HCSPARAMS2 0x08u
#define XHCI_CAP_HCCPARAMS1 0x10u
#define XHCI_CAP_DBOFF 0x14u
#define XHCI_CAP_RTSOFF 0x18u

#define XHCI_OP_USBCMD 0x00u
#define XHCI_OP_USBSTS 0x04u
#define XHCI_OP_PAGESIZE 0x08u
#define XHCI_OP_CRCR 0x18u
#define XHCI_OP_DCBAAP 0x30u
#define XHCI_OP_CONFIG 0x38u
#define XHCI_OP_PORT_BASE 0x400u
#define XHCI_OP_PORT_STRIDE 0x10u

#define XHCI_USBCMD_RUN 0x00000001u
#define XHCI_USBCMD_HCRST 0x00000002u

#define XHCI_USBSTS_HCH 0x00000001u
#define XHCI_USBSTS_CNR 0x00000800u

#define XHCI_PORTSC_CCS (1u << 0)
#define XHCI_PORTSC_PED (1u << 1)
#define XHCI_PORTSC_PR  (1u << 4)
#define XHCI_PORTSC_PLS_SHIFT 5u
#define XHCI_PORTSC_PLS_MASK (0x0Fu << XHCI_PORTSC_PLS_SHIFT)
#define XHCI_PORTSC_PP  (1u << 9)
#define XHCI_PORTSC_SPEED_SHIFT 10u
#define XHCI_PORTSC_SPEED_MASK (0x0Fu << XHCI_PORTSC_SPEED_SHIFT)
#define XHCI_PORTSC_LWS (1u << 16)
#define XHCI_PORTSC_CSC (1u << 17)
#define XHCI_PORTSC_PEC (1u << 18)
#define XHCI_PORTSC_WRC (1u << 19)
#define XHCI_PORTSC_OCC (1u << 20)
#define XHCI_PORTSC_PRC (1u << 21)
#define XHCI_PORTSC_PLC (1u << 22)
#define XHCI_PORTSC_CEC (1u << 23)
#define XHCI_PORTSC_WPR (1u << 31)

#define XHCI_PORTSC_CHANGE_BITS \
    (XHCI_PORTSC_CSC | XHCI_PORTSC_PEC | XHCI_PORTSC_WRC | \
     XHCI_PORTSC_OCC | XHCI_PORTSC_PRC | XHCI_PORTSC_PLC | \
     XHCI_PORTSC_CEC)

#define XHCI_TRB_CYCLE (1u << 0)
#define XHCI_TRB_TC    (1u << 1)
#define XHCI_TRB_ISP   (1u << 2)
#define XHCI_TRB_CHAIN (1u << 4)
#define XHCI_TRB_IOC   (1u << 5)
#define XHCI_TRB_IDT   (1u << 6)
#define XHCI_TRB_TYPE_SHIFT 10u
#define XHCI_TRB_DIR_IN (1u << 16)

#define XHCI_TRB_TYPE_NORMAL 1u
#define XHCI_TRB_TYPE_SETUP_STAGE 2u
#define XHCI_TRB_TYPE_DATA_STAGE 3u
#define XHCI_TRB_TYPE_STATUS_STAGE 4u
#define XHCI_TRB_TYPE_LINK 6u
#define XHCI_TRB_TYPE_TRANSFER_NOOP 8u
#define XHCI_TRB_TYPE_ENABLE_SLOT 9u
#define XHCI_TRB_TYPE_DISABLE_SLOT 10u
#define XHCI_TRB_TYPE_ADDRESS_DEVICE 11u
#define XHCI_TRB_TYPE_CONFIGURE_ENDPOINT 12u
#define XHCI_TRB_TYPE_EVALUATE_CONTEXT 13u
#define XHCI_TRB_TYPE_RESET_ENDPOINT 14u
#define XHCI_TRB_TYPE_STOP_ENDPOINT 15u
#define XHCI_TRB_TYPE_SET_TR_DEQUEUE 16u
#define XHCI_TRB_TYPE_RESET_DEVICE 17u
#define XHCI_TRB_TYPE_TRANSFER_EVENT 32u
#define XHCI_TRB_TYPE_COMMAND_COMPLETION 33u
#define XHCI_TRB_TYPE_PORT_STATUS_CHANGE 34u

#define XHCI_COMPLETION_SUCCESS 1u
#define XHCI_COMPLETION_USB_TRANSACTION_ERROR 4u
#define XHCI_COMPLETION_STALL_ERROR 6u
#define XHCI_COMPLETION_ENDPOINT_NOT_ENABLED 12u
#define XHCI_COMPLETION_SHORT_PACKET 13u
#define XHCI_COMPLETION_CONTEXT_STATE_ERROR 19u

#define XHCI_ENDPOINT_STATE_DISABLED 0u
#define XHCI_ENDPOINT_STATE_RUNNING  1u
#define XHCI_ENDPOINT_STATE_HALTED   2u
#define XHCI_ENDPOINT_STATE_STOPPED  3u
#define XHCI_ENDPOINT_STATE_ERROR    4u

#define XHCI_ENDPOINT_TYPE_BULK_OUT 2u
#define XHCI_ENDPOINT_TYPE_CONTROL 4u
#define XHCI_ENDPOINT_TYPE_BULK_IN 6u

#define USB_DESCRIPTOR_DEVICE 1u
#define USB_DESCRIPTOR_CONFIGURATION 2u
#define USB_DESCRIPTOR_STRING 3u
#define USB_DESCRIPTOR_INTERFACE 4u
#define USB_DESCRIPTOR_ENDPOINT 5u
#define USB_DESCRIPTOR_SS_ENDPOINT_COMPANION 48u

#define USB_REQUEST_CLEAR_FEATURE 1u
#define USB_REQUEST_GET_DESCRIPTOR 6u
#define USB_REQUEST_SET_CONFIGURATION 9u
#define USB_REQUEST_SET_INTERFACE 11u
#define USB_FEATURE_ENDPOINT_HALT 0u

#define XHCI_EXT_CAP_LEGACY 1u
#define XHCI_EXT_CAP_SUPPORTED_PROTOCOL 2u

#define XHCI_LEGACY_BIOS_OWNED (1u << 16)
#define XHCI_LEGACY_OS_OWNED   (1u << 24)

enum
{
    XHCI_ERROR_NONE = 0u,
    XHCI_ERROR_BAD_BAR,
    XHCI_ERROR_PCI_ENABLE,
    XHCI_ERROR_BAD_CAPS,
    XHCI_ERROR_RESET_TIMEOUT,
    XHCI_ERROR_PAGE_SIZE,
    XHCI_ERROR_START_TIMEOUT,
    XHCI_ERROR_NOT_READY
};

typedef struct __attribute__((packed, aligned(16)))
{
    u64 parameter;
    u32 status;
    u32 control;
} XhciTrb;

typedef struct __attribute__((packed, aligned(16)))
{
    u64 ring_segment_base;
    u32 ring_segment_size;
    u32 reserved;
} XhciErstEntry;

typedef struct
{
    bool used;
    bool running;
    bool init_attempted;
    bool init_failed;
    u8 last_error;
    u8 index;

    PciDevice pci;
    u32 mmio_base;
    u8 cap_length;
    u16 hci_version;
    u8 max_slots;
    u8 enabled_slots;
    u8 max_ports;
    u8 context_size;
    u16 scratchpad_count;
    bool port_power_control;

    u32 operational_offset;
    u32 doorbell_offset;
    u32 runtime_offset;

    u16 command_enqueue;
    u16 event_dequeue;
    u8 command_cycle;
    u8 event_cycle;

    u8 port_protocol_major[256];
    u8 port_protocol_minor[256];
    u8 port_slot[256];
    bool port_has_usb[256];
    u32 port_usb_id[256];

    /* Root-hub state is serviced continuously instead of only by manual
     * rescans. A connection is debounced before enumeration. */
    bool port_reprobe_pending[256];
    u64 port_reprobe_at_ms[256];
    u8 port_reprobe_attempts[256];

    u32 hotplug_events;
    u32 disconnect_events;
    u32 endpoint_recoveries;
    u32 transfer_errors;
} XhciController;

typedef struct
{
    bool used;
    u8 controller_index;
    u8 port_number;
    u8 slot_id;
    u8 speed_id;
    u16 endpoint0_max_packet;
    u16 transfer_enqueue;
    u8 transfer_cycle;

    bool bulk_configured;
    u8 bulk_out_endpoint;
    u8 bulk_in_endpoint;
    u8 bulk_out_dci;
    u8 bulk_in_dci;
    u16 bulk_out_enqueue;
    u16 bulk_in_enqueue;
    u8 bulk_out_cycle;
    u8 bulk_in_cycle;

    /* One persistent asynchronous Bulk-IN request is enough for polling
     * network class drivers such as RNDIS. */
    bool bulk_in_async_pending;
    bool bulk_in_async_complete;
    u64 bulk_in_async_buffer;
    usize bulk_in_async_length;
    XhciTrb bulk_in_async_event;

    u32 usb_device_id;
} XhciDeviceRuntime;

static XhciController controllers[XHCI_MAX_CONTROLLERS];
static usize active_controller_count = 0u;

static XhciDeviceRuntime runtime_devices[XHCI_MAX_TRACKED_DEVICES];

static void xhci_prepare_normal_transfer_ring(XhciTrb* ring);
static void xhci_advance_normal_transfer_ring(
    XhciTrb* ring,
    u16* enqueue,
    u8* cycle
);


static u64 dcbaa[XHCI_MAX_CONTROLLERS][XHCI_DCBAA_ENTRIES]
    __attribute__((aligned(64)));

static XhciTrb command_rings[XHCI_MAX_CONTROLLERS][XHCI_COMMAND_TRBS]
    __attribute__((aligned(64)));

static XhciTrb event_rings[XHCI_MAX_CONTROLLERS][XHCI_EVENT_TRBS]
    __attribute__((aligned(64)));

static XhciErstEntry erst_entries[XHCI_MAX_CONTROLLERS]
    __attribute__((aligned(64)));

static u64 scratchpad_pointers[XHCI_MAX_CONTROLLERS][XHCI_MAX_SCRATCHPADS]
    __attribute__((aligned(64)));

static u8 scratchpad_buffers[XHCI_MAX_CONTROLLERS][XHCI_MAX_SCRATCHPADS][4096]
    __attribute__((aligned(4096)));

static u8 device_contexts[XHCI_MAX_TRACKED_DEVICES][XHCI_DEVICE_CONTEXT_BYTES]
    __attribute__((aligned(64)));

static u8 input_contexts[XHCI_MAX_TRACKED_DEVICES][XHCI_INPUT_CONTEXT_BYTES]
    __attribute__((aligned(64)));

static XhciTrb transfer_rings[XHCI_MAX_TRACKED_DEVICES][XHCI_TRANSFER_TRBS]
    __attribute__((aligned(64)));

static u8 control_buffers[XHCI_MAX_TRACKED_DEVICES][XHCI_CONTROL_BUFFER_SIZE]
    __attribute__((aligned(64)));

static XhciTrb bulk_out_rings[XHCI_MAX_TRACKED_DEVICES][XHCI_TRANSFER_TRBS]
    __attribute__((aligned(64)));

static XhciTrb bulk_in_rings[XHCI_MAX_TRACKED_DEVICES][XHCI_TRANSFER_TRBS]
    __attribute__((aligned(64)));

static inline void xhci_barrier(void)
{
    __asm__ volatile("" ::: "memory");
}

static inline u64 xhci_physical(const void* pointer)
{
    return (u64)(usize)pointer;
}

static inline volatile u8* xhci_mmio8(
    const XhciController* controller,
    u32 offset
)
{
    return
        (volatile u8*)(usize)(
            controller->mmio_base + offset
        );
}

static inline volatile u16* xhci_mmio16(
    const XhciController* controller,
    u32 offset
)
{
    return
        (volatile u16*)(usize)(
            controller->mmio_base + offset
        );
}

static inline volatile u32* xhci_mmio32(
    const XhciController* controller,
    u32 offset
)
{
    return
        (volatile u32*)(usize)(
            controller->mmio_base + offset
        );
}

static u8 xhci_read8(
    const XhciController* controller,
    u32 offset
)
{
    return *xhci_mmio8(controller, offset);
}

static u16 xhci_read16(
    const XhciController* controller,
    u32 offset
)
{
    return *xhci_mmio16(controller, offset);
}

static u32 xhci_read32(
    const XhciController* controller,
    u32 offset
)
{
    return *xhci_mmio32(controller, offset);
}

static void xhci_write32(
    const XhciController* controller,
    u32 offset,
    u32 value
)
{
    *xhci_mmio32(controller, offset) = value;
    xhci_barrier();
}

static void xhci_write64(
    const XhciController* controller,
    u32 offset,
    u64 value
)
{
    /*
     * RootOS is still i386.  xHCI registers are written as two dwords.
     * DMA structures are deliberately allocated below 4 GiB today.
     */
    /*
     * xHCI 64-bit pointer registers use low-dword then high-dword ordering
     * when software performs two 32-bit MMIO writes.
     */
    xhci_write32(
        controller,
        offset,
        (u32)(value & 0xFFFFFFFFu)
    );

    xhci_write32(
        controller,
        offset + 4u,
        (u32)(value >> 32)
    );
}

static bool xhci_interrupts_enabled(void)
{
    u32 flags;

    __asm__ volatile(
        "pushfl\n"
        "popl %0"
        : "=r"(flags)
        :
        : "memory"
    );

    return (flags & (1u << 9)) != 0u;
}

static void xhci_wait_yield(void)
{
    for (u32 i = 0u; i < XHCI_WAIT_PAUSES_PER_ROUND; i++)
    {
        __asm__ volatile("pause");
    }

    if (xhci_interrupts_enabled())
    {
        /* PIT wakes us even though xHCI completion is polled in v0.44. */
        __asm__ volatile("hlt");
    }
}

static bool xhci_wait_bits(
    const XhciController* controller,
    u32 offset,
    u32 mask,
    u32 expected
)
{
    for (u32 round = 0u; round < XHCI_WAIT_ROUNDS; round++)
    {
        for (u32 i = 0u; i < XHCI_WAIT_PAUSES_PER_ROUND; i++)
        {
            if (
                (xhci_read32(controller, offset) & mask)
                ==
                expected
            )
            {
                return true;
            }

            __asm__ volatile("pause");
        }

        xhci_wait_yield();
    }

    return false;
}

static u32 xhci_operational(
    const XhciController* controller,
    u32 register_offset
)
{
    return controller->operational_offset + register_offset;
}

static u32 xhci_port_offset(
    const XhciController* controller,
    u8 port_number
)
{
    return
        controller->operational_offset
        +
        XHCI_OP_PORT_BASE
        +
        ((u32)(port_number - 1u) * XHCI_OP_PORT_STRIDE);
}

static u32 xhci_port_write_base(u32 portsc)
{
    /*
     * Change bits are RW1C and PED is disabled by writing one.  Keep both zero
     * unless the operation explicitly wants a write to them.
     */
    u32 value = portsc;
    value &= ~XHCI_PORTSC_CHANGE_BITS;
    value &= ~XHCI_PORTSC_PED;
    value &= ~XHCI_PORTSC_LWS;
    return value;
}

static void xhci_parse_extended_capabilities(
    XhciController* controller,
    u32 hccparams1
)
{
    u32 offset =
        ((hccparams1 >> 16) & 0xFFFFu)
        *
        4u;

    u32 visited = 0u;

    while (offset != 0u && visited < 64u)
    {
        u32 header = xhci_read32(controller, offset);
        u8 capability_id = (u8)(header & 0xFFu);
        u8 next = (u8)((header >> 8) & 0xFFu);

        if (capability_id == XHCI_EXT_CAP_LEGACY)
        {
            if ((header & XHCI_LEGACY_BIOS_OWNED) != 0u)
            {
                xhci_write32(
                    controller,
                    offset,
                    header | XHCI_LEGACY_OS_OWNED
                );

                for (u32 round = 0u; round < XHCI_WAIT_ROUNDS; round++)
                {
                    u32 legacy = xhci_read32(controller, offset);

                    if (
                        (legacy & XHCI_LEGACY_BIOS_OWNED)
                        ==
                        0u
                    )
                    {
                        break;
                    }

                    xhci_wait_yield();
                }
            }

            /* Disable legacy SMI routing after ownership handoff. */
            xhci_write32(controller, offset + 4u, 0u);
        }
        else if (capability_id == XHCI_EXT_CAP_SUPPORTED_PROTOCOL)
        {
            u8 major = (u8)((header >> 24) & 0xFFu);
            u8 minor = (u8)((header >> 16) & 0xFFu);
            u32 ports = xhci_read32(controller, offset + 8u);
            u8 first = (u8)(ports & 0xFFu);
            u8 count = (u8)((ports >> 8) & 0xFFu);

            for (u16 p = 0u; p < count; p++)
            {
                u16 port = (u16)first + p;

                if (port == 0u || port > controller->max_ports)
                {
                    continue;
                }

                controller->port_protocol_major[port - 1u] = major;
                controller->port_protocol_minor[port - 1u] = minor;
            }
        }

        if (next == 0u)
        {
            break;
        }

        offset += (u32)next * 4u;
        visited++;
    }
}

static u16 xhci_max_scratchpads(u32 hcsparams2)
{
    u16 high = (u16)((hcsparams2 >> 27) & 0x1Fu);
    u16 low = (u16)((hcsparams2 >> 21) & 0x1Fu);

    return (u16)((high << 5) | low);
}

static bool xhci_pop_event(
    XhciController* controller,
    XhciTrb* output
)
{
    volatile XhciTrb* event =
        &event_rings[controller->index][controller->event_dequeue];

    u32 control = event->control;

    if (
        (control & XHCI_TRB_CYCLE)
        !=
        (u32)controller->event_cycle
    )
    {
        return false;
    }

    output->parameter = event->parameter;
    output->status = event->status;
    output->control = control;

    controller->event_dequeue++;

    if (controller->event_dequeue >= XHCI_EVENT_TRBS)
    {
        controller->event_dequeue = 0u;
        controller->event_cycle ^= 1u;
    }

    u64 dequeue =
        xhci_physical(
            &event_rings[
                controller->index
            ][
                controller->event_dequeue
            ]
        );

    /* EHB=1 acknowledges Event Handler Busy while updating ERDP. */
    xhci_write64(
        controller,
        controller->runtime_offset + 0x20u + 0x18u,
        dequeue | (1ull << 3)
    );

    return true;
}

static u8 xhci_event_type(const XhciTrb* event)
{
    return (u8)(
        (event->control >> XHCI_TRB_TYPE_SHIFT)
        &
        0x3Fu
    );
}

static u8 xhci_completion_code(const XhciTrb* event)
{
    return (u8)((event->status >> 24) & 0xFFu);
}

static bool xhci_capture_async_transfer_event(
    XhciController* controller,
    const XhciTrb* event
)
{
    if (
        controller == NULL
        || event == NULL
        || xhci_event_type(event) != XHCI_TRB_TYPE_TRANSFER_EVENT
    )
    {
        return false;
    }

    u8 slot_id = (u8)((event->control >> 24) & 0xFFu);
    u8 endpoint_id = (u8)((event->control >> 16) & 0x1Fu);

    for (usize i = 0u; i < XHCI_MAX_TRACKED_DEVICES; i++)
    {
        XhciDeviceRuntime* runtime = &runtime_devices[i];
        if (
            !runtime->used
            || runtime->controller_index != controller->index
            || !runtime->bulk_in_async_pending
            || runtime->slot_id != slot_id
            || runtime->bulk_in_dci != endpoint_id
        )
        {
            continue;
        }

        runtime->bulk_in_async_event = *event;
        runtime->bulk_in_async_complete = true;
        return true;
    }

    return false;
}

static void xhci_collect_idle_events(
    XhciController* controller,
    u32 limit
)
{
    if (controller == NULL)
        return;

    for (u32 count = 0u; count < limit; count++)
    {
        XhciTrb event;
        if (!xhci_pop_event(controller, &event))
            break;

        if (xhci_capture_async_transfer_event(controller, &event))
            continue;

        if (xhci_event_type(&event) == XHCI_TRB_TYPE_PORT_STATUS_CHANGE)
            controller->hotplug_events++;
        /* Other events here are stale completions from a bounded synchronous
         * request. No independent async command queue exists yet. */
    }
}


static bool xhci_wait_command_completion(
    XhciController* controller,
    u64 command_address,
    XhciTrb* completion
)
{
    for (u32 round = 0u; round < XHCI_WAIT_ROUNDS; round++)
    {
        for (u32 poll = 0u; poll < XHCI_WAIT_PAUSES_PER_ROUND; poll++)
        {
            XhciTrb event;

            if (!xhci_pop_event(controller, &event))
            {
                __asm__ volatile("pause");
                continue;
            }

            if (xhci_capture_async_transfer_event(controller, &event))
                continue;

            if (
                xhci_event_type(&event) == XHCI_TRB_TYPE_COMMAND_COMPLETION
                &&
                (event.parameter & ~0x0Full)
                ==
                (command_address & ~0x0Full)
            )
            {
                *completion = event;
                return true;
            }
        }

        xhci_wait_yield();
    }

    return false;
}

static bool xhci_wait_transfer_completion(
    XhciController* controller,
    u8 slot_id,
    u8 endpoint_id,
    XhciTrb* completion
)
{
    for (u32 round = 0u; round < XHCI_WAIT_ROUNDS; round++)
    {
        for (u32 poll = 0u; poll < XHCI_WAIT_PAUSES_PER_ROUND; poll++)
        {
            XhciTrb event;

            if (!xhci_pop_event(controller, &event))
            {
                __asm__ volatile("pause");
                continue;
            }

            if (xhci_event_type(&event) != XHCI_TRB_TYPE_TRANSFER_EVENT)
            {
                continue;
            }

            u8 event_slot =
                (u8)((event.control >> 24) & 0xFFu);

            u8 event_endpoint =
                (u8)((event.control >> 16) & 0x1Fu);

            if (
                event_slot == slot_id
                &&
                event_endpoint == endpoint_id
            )
            {
                *completion = event;
                return true;
            }

            (void)xhci_capture_async_transfer_event(controller, &event);
        }

        xhci_wait_yield();
    }

    return false;
}

static void xhci_prepare_command_ring(XhciController* controller)
{
    XhciTrb* ring = &command_rings[controller->index][0];
    root_memzero(ring, sizeof(XhciTrb) * XHCI_COMMAND_TRBS);

    XhciTrb* link = &ring[XHCI_COMMAND_TRBS - 1u];
    link->parameter = xhci_physical(ring);
    link->status = 0u;
    link->control =
        (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT)
        | XHCI_TRB_TC
        | XHCI_TRB_CYCLE;
}

static void xhci_advance_command_ring(XhciController* controller)
{
    controller->command_enqueue++;

    if (controller->command_enqueue >= XHCI_COMMAND_TRBS - 1u)
    {
        XhciTrb* ring = &command_rings[controller->index][0];
        XhciTrb* link = &ring[XHCI_COMMAND_TRBS - 1u];

        link->parameter = xhci_physical(ring);
        link->status = 0u;
        link->control =
            (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT)
            | XHCI_TRB_TC
            | (u32)controller->command_cycle;

        xhci_barrier();
        controller->command_enqueue = 0u;
        controller->command_cycle ^= 1u;
    }
}

static bool xhci_command(
    XhciController* controller,
    u64 parameter,
    u32 status,
    u32 control,
    XhciTrb* completion
)
{
    if (controller == NULL || completion == NULL)
        return false;

    XhciTrb* command =
        &command_rings[controller->index][controller->command_enqueue];

    command->parameter = parameter;
    command->status = status;
    command->control = control | (u32)controller->command_cycle;

    xhci_barrier();

    u64 command_address = xhci_physical(command);
    xhci_advance_command_ring(controller);

    /* Doorbell 0 is the command ring. */
    xhci_write32(controller, controller->doorbell_offset, 0u);

    return xhci_wait_command_completion(
        controller,
        command_address,
        completion
    );
}

static bool xhci_enable_slot(
    XhciController* controller,
    u8* slot_id
)
{
    XhciTrb completion;

    if (
        !xhci_command(
            controller,
            0u,
            0u,
            XHCI_TRB_TYPE_ENABLE_SLOT << XHCI_TRB_TYPE_SHIFT,
            &completion
        )
    )
    {
        return false;
    }

    if (xhci_completion_code(&completion) != XHCI_COMPLETION_SUCCESS)
    {
        return false;
    }

    u8 assigned =
        (u8)((completion.control >> 24) & 0xFFu);

    if (
        assigned == 0u
        ||
        assigned > controller->enabled_slots
    )
    {
        return false;
    }

    *slot_id = assigned;
    return true;
}

static void xhci_disable_slot(
    XhciController* controller,
    u8 slot_id
)
{
    XhciTrb completion;

    (void)xhci_command(
        controller,
        0u,
        0u,
        (XHCI_TRB_TYPE_DISABLE_SLOT << XHCI_TRB_TYPE_SHIFT)
        |
        ((u32)slot_id << 24),
        &completion
    );
}

static void* xhci_input_context(
    usize runtime_index,
    u8 context_size,
    u8 context_index
)
{
    return
        &input_contexts[runtime_index][
            (usize)context_size
            *
            context_index
        ];
}

static void* xhci_device_context(
    usize runtime_index,
    u8 context_size,
    u8 context_index
)
{
    return
        &device_contexts[runtime_index][
            (usize)context_size
            *
            context_index
        ];
}

static u16 xhci_initial_ep0_packet(u8 speed_id)
{
    switch (speed_id)
    {
        case 1u: return 8u;   /* full */
        case 2u: return 8u;   /* low */
        case 3u: return 64u;  /* high */
        case 4u: return 512u; /* super */
        case 5u: return 512u; /* super+ common PSI */
        default: return 8u;
    }
}

static UsbSpeed xhci_usb_speed(u8 speed_id)
{
    switch (speed_id)
    {
        case 1u: return USB_SPEED_FULL;
        case 2u: return USB_SPEED_LOW;
        case 3u: return USB_SPEED_HIGH;
        case 4u: return USB_SPEED_SUPER;
        case 5u: return USB_SPEED_SUPER_PLUS;
        default: return USB_SPEED_UNKNOWN;
    }
}

static int xhci_alloc_runtime_device(void)
{
    for (usize i = 0u; i < XHCI_MAX_TRACKED_DEVICES; i++)
    {
        if (!runtime_devices[i].used)
        {
            root_memzero(
                &runtime_devices[i],
                sizeof(runtime_devices[i])
            );

            runtime_devices[i].used = true;
            runtime_devices[i].transfer_cycle = 1u;
            runtime_devices[i].bulk_out_cycle = 1u;
            runtime_devices[i].bulk_in_cycle = 1u;
            return (int)i;
        }
    }

    return -1;
}

static void xhci_free_runtime_device(usize index)
{
    if (index >= XHCI_MAX_TRACKED_DEVICES)
    {
        return;
    }

    root_memzero(
        &runtime_devices[index],
        sizeof(runtime_devices[index])
    );
}

static bool xhci_address_device(
    XhciController* controller,
    usize runtime_index,
    u8 port_number,
    u8 speed_id,
    u8 slot_id
)
{
    XhciDeviceRuntime* runtime =
        &runtime_devices[runtime_index];

    u8 context_size = controller->context_size;

    root_memzero(
        input_contexts[runtime_index],
        XHCI_INPUT_CONTEXT_BYTES
    );

    root_memzero(
        device_contexts[runtime_index],
        XHCI_DEVICE_CONTEXT_BYTES
    );

    xhci_prepare_normal_transfer_ring(
        &transfer_rings[runtime_index][0]
    );

    xhci_prepare_normal_transfer_ring(
        &bulk_out_rings[runtime_index][0]
    );

    xhci_prepare_normal_transfer_ring(
        &bulk_in_rings[runtime_index][0]
    );

    u32* input_control =
        (u32*)xhci_input_context(
            runtime_index,
            context_size,
            0u
        );

    /* Add Slot Context + Endpoint 0 Context. */
    input_control[1] = 0x00000003u;

    u32* slot =
        (u32*)xhci_input_context(
            runtime_index,
            context_size,
            1u
        );

    slot[0] =
        ((u32)speed_id << 20)
        |
        (1u << 27);

    slot[1] =
        (u32)port_number << 16;

    u16 max_packet =
        xhci_initial_ep0_packet(speed_id);

    u32* endpoint0 =
        (u32*)xhci_input_context(
            runtime_index,
            context_size,
            2u
        );

    endpoint0[1] =
        (3u << 1)
        |
        (XHCI_ENDPOINT_TYPE_CONTROL << 3)
        |
        ((u32)max_packet << 16);

    u64 transfer_ring_address =
        xhci_physical(
            &transfer_rings[runtime_index][0]
        );

    endpoint0[2] =
        (u32)(transfer_ring_address & 0xFFFFFFFFu)
        |
        1u;

    endpoint0[3] =
        (u32)(transfer_ring_address >> 32);

    endpoint0[4] = 8u;

    dcbaa[controller->index][slot_id] =
        xhci_physical(
            &device_contexts[runtime_index][0]
        );

    xhci_barrier();

    XhciTrb completion;

    if (
        !xhci_command(
            controller,
            xhci_physical(
                &input_contexts[runtime_index][0]
            ),
            0u,
            (XHCI_TRB_TYPE_ADDRESS_DEVICE << XHCI_TRB_TYPE_SHIFT)
            |
            ((u32)slot_id << 24),
            &completion
        )
    )
    {
        return false;
    }

    if (xhci_completion_code(&completion) != XHCI_COMPLETION_SUCCESS)
    {
        return false;
    }

    runtime->controller_index = controller->index;
    runtime->port_number = port_number;
    runtime->slot_id = slot_id;
    runtime->speed_id = speed_id;
    runtime->endpoint0_max_packet = max_packet;
    runtime->transfer_enqueue = 0u;
    runtime->transfer_cycle = 1u;

    return true;
}

static bool xhci_transfer_enqueue(
    usize runtime_index,
    u64 parameter,
    u32 status,
    u32 control
)
{
    if (runtime_index >= XHCI_MAX_TRACKED_DEVICES)
        return false;

    XhciDeviceRuntime* runtime = &runtime_devices[runtime_index];
    if (!runtime->used || runtime->transfer_enqueue >= XHCI_TRANSFER_TRBS - 1u)
        return false;

    XhciTrb* ring = &transfer_rings[runtime_index][0];
    XhciTrb* trb = &ring[runtime->transfer_enqueue];

    trb->parameter = parameter;
    trb->status = status;
    trb->control = control | (u32)runtime->transfer_cycle;

    xhci_advance_normal_transfer_ring(
        ring,
        &runtime->transfer_enqueue,
        &runtime->transfer_cycle
    );

    return true;
}

static bool xhci_control_ensure_space(
    usize runtime_index,
    u16 trbs_needed
)
{
    if (runtime_index >= XHCI_MAX_TRACKED_DEVICES || trbs_needed == 0u)
        return false;

    XhciDeviceRuntime* runtime = &runtime_devices[runtime_index];
    if (!runtime->used || trbs_needed >= XHCI_TRANSFER_TRBS - 1u)
        return false;

    /* Keep one complete Control Transfer TD on one side of the Link TRB.
     * When too close to the end, queue transfer No-op TRBs until the producer
     * wraps. This prevents Setup/Data/Status from being split by a Link TRB,
     * while still making EP0's ring reusable indefinitely. */
    while (
        runtime->transfer_enqueue + trbs_needed
        > XHCI_TRANSFER_TRBS - 1u
    )
    {
        if (
            !xhci_transfer_enqueue(
                runtime_index,
                0u,
                0u,
                XHCI_TRB_TYPE_TRANSFER_NOOP << XHCI_TRB_TYPE_SHIFT
            )
        )
        {
            return false;
        }
    }

    return true;
}

static int xhci_find_runtime_by_usb_id(u32 usb_device_id)
{
    for (usize i = 0u; i < XHCI_MAX_TRACKED_DEVICES; i++)
    {
        if (
            runtime_devices[i].used
            && runtime_devices[i].usb_device_id == usb_device_id
        )
        {
            return (int)i;
        }
    }

    return -1;
}

static u8 xhci_endpoint_dci(u8 endpoint_address)
{
    u8 number = endpoint_address & 0x0Fu;

    if (number == 0u)
    {
        return 1u;
    }

    return
        (u8)(number * 2u)
        +
        ((endpoint_address & 0x80u) != 0u ? 1u : 0u);
}

static bool xhci_control_no_data(
    XhciController* controller,
    usize runtime_index,
    u8 request_type,
    u8 request,
    u16 value,
    u16 index
)
{
    XhciDeviceRuntime* runtime = &runtime_devices[runtime_index];

    if (!xhci_control_ensure_space(runtime_index, 2u))
        return false;

    u64 setup =
        (u64)request_type
        |
        ((u64)request << 8)
        |
        ((u64)value << 16)
        |
        ((u64)index << 32);

    if (
        !xhci_transfer_enqueue(
            runtime_index,
            setup,
            8u,
            (XHCI_TRB_TYPE_SETUP_STAGE << XHCI_TRB_TYPE_SHIFT)
            |
            XHCI_TRB_IDT
            |
            XHCI_TRB_CHAIN
        )
    )
    {
        return false;
    }

    if (
        !xhci_transfer_enqueue(
            runtime_index,
            0u,
            0u,
            (XHCI_TRB_TYPE_STATUS_STAGE << XHCI_TRB_TYPE_SHIFT)
            |
            XHCI_TRB_DIR_IN
            |
            XHCI_TRB_IOC
        )
    )
    {
        return false;
    }

    xhci_barrier();

    xhci_write32(
        controller,
        controller->doorbell_offset
        + ((u32)runtime->slot_id * 4u),
        1u
    );

    XhciTrb completion;

    if (
        !xhci_wait_transfer_completion(
            controller,
            runtime->slot_id,
            1u,
            &completion
        )
    )
    {
        return false;
    }

    return xhci_completion_code(&completion) == XHCI_COMPLETION_SUCCESS;
}

static void xhci_prepare_normal_transfer_ring(
    XhciTrb* ring
)
{
    root_memzero(ring, sizeof(XhciTrb) * XHCI_TRANSFER_TRBS);

    XhciTrb* link = &ring[XHCI_TRANSFER_TRBS - 1u];
    link->parameter = xhci_physical(ring);
    link->status = 0u;
    link->control =
        (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT)
        | XHCI_TRB_TC
        | XHCI_TRB_CYCLE;
}

static void xhci_advance_normal_transfer_ring(
    XhciTrb* ring,
    u16* enqueue,
    u8* cycle
)
{
    (*enqueue)++;

    if (*enqueue >= XHCI_TRANSFER_TRBS - 1u)
    {
        XhciTrb* link = &ring[XHCI_TRANSFER_TRBS - 1u];
        link->parameter = xhci_physical(ring);
        link->status = 0u;
        link->control =
            (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT)
            | XHCI_TRB_TC
            | (u32)(*cycle);

        xhci_barrier();
        *enqueue = 0u;
        *cycle ^= 1u;
    }
}

static void xhci_prepare_bulk_endpoint_context(
    u32* endpoint,
    u8 endpoint_type,
    u16 max_packet,
    u8 max_burst,
    u64 ring_address
)
{
    root_memzero(endpoint, 8u * sizeof(u32));

    endpoint[1] =
        (3u << 1)
        |
        ((u32)endpoint_type << 3)
        |
        ((u32)max_burst << 8)
        |
        ((u32)max_packet << 16);

    endpoint[2] =
        (u32)(ring_address & 0xFFFFFFFFu)
        |
        1u;

    endpoint[3] = (u32)(ring_address >> 32);

    /* Average TRB length. A conservative packet-sized value works for BOT. */
    endpoint[4] = max_packet;
}

static bool xhci_get_descriptor(
    XhciController* controller,
    usize runtime_index,
    u8 descriptor_type,
    u8 descriptor_index,
    u16 language_id,
    u16 length,
    const u8** output
)
{
    if (
        length == 0u
        ||
        length > XHCI_CONTROL_BUFFER_SIZE
    )
    {
        return false;
    }

    XhciDeviceRuntime* runtime =
        &runtime_devices[runtime_index];

    if (!xhci_control_ensure_space(runtime_index, 3u))
        return false;

    u8* buffer = control_buffers[runtime_index];

    root_memzero(
        buffer,
        XHCI_CONTROL_BUFFER_SIZE
    );

    u16 value =
        ((u16)descriptor_type << 8)
        |
        descriptor_index;

    u64 setup =
        0x80ull
        |
        ((u64)USB_REQUEST_GET_DESCRIPTOR << 8)
        |
        ((u64)value << 16)
        |
        ((u64)language_id << 32)
        |
        ((u64)length << 48);

    if (
        !xhci_transfer_enqueue(
            runtime_index,
            setup,
            8u,
            (XHCI_TRB_TYPE_SETUP_STAGE << XHCI_TRB_TYPE_SHIFT)
            |
            XHCI_TRB_IDT
            |
            XHCI_TRB_CHAIN
            |
            (3u << 16) /* TRT = IN Data Stage */
        )
    )
    {
        return false;
    }

    if (
        !xhci_transfer_enqueue(
            runtime_index,
            xhci_physical(buffer),
            length,
            (XHCI_TRB_TYPE_DATA_STAGE << XHCI_TRB_TYPE_SHIFT)
            |
            XHCI_TRB_DIR_IN
            |
            XHCI_TRB_CHAIN
        )
    )
    {
        return false;
    }

    if (
        !xhci_transfer_enqueue(
            runtime_index,
            0u,
            0u,
            (XHCI_TRB_TYPE_STATUS_STAGE << XHCI_TRB_TYPE_SHIFT)
            |
            XHCI_TRB_IOC
        )
    )
    {
        return false;
    }

    xhci_barrier();

    /* Doorbell index is Slot ID; target 1 is Default Control Endpoint. */
    xhci_write32(
        controller,
        controller->doorbell_offset
        +
        ((u32)runtime->slot_id * 4u),
        1u
    );

    XhciTrb completion;

    if (
        !xhci_wait_transfer_completion(
            controller,
            runtime->slot_id,
            1u,
            &completion
        )
    )
    {
        return false;
    }

    u8 code = xhci_completion_code(&completion);

    if (
        code != XHCI_COMPLETION_SUCCESS
        &&
        code != XHCI_COMPLETION_SHORT_PACKET
    )
    {
        return false;
    }

    *output = buffer;
    return true;
}

static usize xhci_utf8_append(
    char* output,
    usize capacity,
    usize offset,
    u32 codepoint
)
{
    if (capacity == 0u || offset >= capacity - 1u)
    {
        return offset;
    }

    if (codepoint <= 0x7Fu)
    {
        output[offset++] = (char)codepoint;
    }
    else if (codepoint <= 0x7FFu)
    {
        if (offset + 2u >= capacity)
        {
            return offset;
        }

        output[offset++] = (char)(0xC0u | (codepoint >> 6));
        output[offset++] = (char)(0x80u | (codepoint & 0x3Fu));
    }
    else if (codepoint <= 0xFFFFu)
    {
        if (offset + 3u >= capacity)
        {
            return offset;
        }

        output[offset++] = (char)(0xE0u | (codepoint >> 12));
        output[offset++] = (char)(0x80u | ((codepoint >> 6) & 0x3Fu));
        output[offset++] = (char)(0x80u | (codepoint & 0x3Fu));
    }
    else if (codepoint <= 0x10FFFFu)
    {
        if (offset + 4u >= capacity)
        {
            return offset;
        }

        output[offset++] = (char)(0xF0u | (codepoint >> 18));
        output[offset++] = (char)(0x80u | ((codepoint >> 12) & 0x3Fu));
        output[offset++] = (char)(0x80u | ((codepoint >> 6) & 0x3Fu));
        output[offset++] = (char)(0x80u | (codepoint & 0x3Fu));
    }

    output[offset] = '\0';
    return offset;
}

static void xhci_decode_usb_string(
    const u8* descriptor,
    char* output,
    usize capacity
)
{
    if (output == NULL || capacity == 0u)
    {
        return;
    }

    output[0] = '\0';

    if (
        descriptor == NULL
        ||
        descriptor[0] < 2u
        ||
        descriptor[1] != USB_DESCRIPTOR_STRING
    )
    {
        return;
    }

    usize length = descriptor[0];
    usize offset = 0u;

    for (usize i = 2u; i + 1u < length; i += 2u)
    {
        u32 codepoint =
            (u32)descriptor[i]
            |
            ((u32)descriptor[i + 1u] << 8);

        if (
            codepoint >= 0xD800u
            &&
            codepoint <= 0xDBFFu
            &&
            i + 3u < length
        )
        {
            u32 low =
                (u32)descriptor[i + 2u]
                |
                ((u32)descriptor[i + 3u] << 8);

            if (low >= 0xDC00u && low <= 0xDFFFu)
            {
                codepoint =
                    0x10000u
                    +
                    ((codepoint - 0xD800u) << 10)
                    +
                    (low - 0xDC00u);

                i += 2u;
            }
        }

        offset = xhci_utf8_append(
            output,
            capacity,
            offset,
            codepoint
        );
    }

    output[offset] = '\0';
}

static void xhci_read_string(
    XhciController* controller,
    usize runtime_index,
    u8 index,
    u16 language_id,
    char* output,
    usize capacity
)
{
    if (output == NULL || capacity == 0u)
    {
        return;
    }

    output[0] = '\0';

    if (index == 0u)
    {
        return;
    }

    const u8* descriptor = NULL;

    if (
        !xhci_get_descriptor(
            controller,
            runtime_index,
            USB_DESCRIPTOR_STRING,
            index,
            language_id,
            255u,
            &descriptor
        )
    )
    {
        return;
    }

    xhci_decode_usb_string(
        descriptor,
        output,
        capacity
    );
}

static u16 xhci_read_language_id(
    XhciController* controller,
    usize runtime_index
)
{
    const u8* descriptor = NULL;

    if (
        !xhci_get_descriptor(
            controller,
            runtime_index,
            USB_DESCRIPTOR_STRING,
            0u,
            0u,
            255u,
            &descriptor
        )
    )
    {
        return 0x0409u;
    }

    if (
        descriptor[0] < 4u
        ||
        descriptor[1] != USB_DESCRIPTOR_STRING
    )
    {
        return 0x0409u;
    }

    return
        (u16)descriptor[2]
        |
        ((u16)descriptor[3] << 8);
}

static void xhci_parse_configuration(
    XhciController* controller,
    usize runtime_index,
    UsbDeviceInfo* device
)
{
    const u8* header = NULL;

    if (
        !xhci_get_descriptor(
            controller,
            runtime_index,
            USB_DESCRIPTOR_CONFIGURATION,
            0u,
            0u,
            9u,
            &header
        )
    )
    {
        return;
    }

    if (
        header[0] < 9u
        ||
        header[1] != USB_DESCRIPTOR_CONFIGURATION
    )
    {
        return;
    }

    u16 total_length =
        (u16)header[2]
        |
        ((u16)header[3] << 8);

    if (total_length < 9u)
    {
        return;
    }

    if (total_length > XHCI_CONTROL_BUFFER_SIZE)
    {
        total_length = XHCI_CONTROL_BUFFER_SIZE;
    }

    const u8* configuration = NULL;

    if (
        !xhci_get_descriptor(
            controller,
            runtime_index,
            USB_DESCRIPTOR_CONFIGURATION,
            0u,
            0u,
            total_length,
            &configuration
        )
    )
    {
        return;
    }

    device->configuration_value = configuration[5];
    device->configuration_attributes = configuration[7];

    u16 power_unit =
        device->speed >= USB_SPEED_SUPER
        ?
        8u
        :
        2u;

    device->max_power_ma =
        (u16)configuration[8]
        *
        power_unit;

    device->interface_count = 0u;
    UsbInterfaceInfo* current_interface = NULL;
    UsbEndpointInfo* current_endpoint = NULL;

    usize offset = 0u;

    while (offset + 2u <= total_length)
    {
        u8 length = configuration[offset];
        u8 type = configuration[offset + 1u];

        if (
            length < 2u
            ||
            offset + length > total_length
        )
        {
            break;
        }

        if (
            type == USB_DESCRIPTOR_INTERFACE
            &&
            length >= 9u
            &&
            device->interface_count < USB_MAX_INTERFACES
        )
        {
            current_interface =
                &device->interfaces[
                    device->interface_count++
                ];

            root_memzero(
                current_interface,
                sizeof(*current_interface)
            );
            current_endpoint = NULL;

            current_interface->number = configuration[offset + 2u];
            current_interface->alternate_setting = configuration[offset + 3u];
            current_interface->declared_endpoint_count = configuration[offset + 4u];
            current_interface->class_code = configuration[offset + 5u];
            current_interface->subclass = configuration[offset + 6u];
            current_interface->protocol = configuration[offset + 7u];
            current_interface->string_index = configuration[offset + 8u];
        }
        else if (
            type == USB_DESCRIPTOR_ENDPOINT
            &&
            length >= 7u
            &&
            current_interface != NULL
            &&
            current_interface->endpoint_count < USB_MAX_ENDPOINTS
        )
        {
            UsbEndpointInfo* endpoint =
                &current_interface->endpoints[
                    current_interface->endpoint_count++
                ];

            root_memzero(endpoint, sizeof(*endpoint));
            endpoint->address = configuration[offset + 2u];
            endpoint->attributes = configuration[offset + 3u];
            endpoint->max_packet_size =
                (
                    (u16)configuration[offset + 4u]
                    |
                    ((u16)configuration[offset + 5u] << 8)
                )
                &
                0x07FFu;
            endpoint->interval = configuration[offset + 6u];
            current_endpoint = endpoint;
        }
        else if (
            type == USB_DESCRIPTOR_SS_ENDPOINT_COMPANION
            && length >= 6u
            && current_endpoint != NULL
        )
        {
            /* USB 3.x section 9.6.7: companion descriptor follows its
             * endpoint descriptor. bMaxBurst is required by xHCI endpoint
             * context for correct SuperSpeed burst scheduling. */
            current_endpoint->max_burst =
                configuration[offset + 2u] > 15u
                ? 15u
                : configuration[offset + 2u];
            current_endpoint->companion_attributes = configuration[offset + 3u];
            current_endpoint->bytes_per_interval =
                (u16)configuration[offset + 4u]
                | ((u16)configuration[offset + 5u] << 8);
        }

        offset += length;
    }
}

static bool xhci_enumerate_port(
    XhciController* controller,
    u8 port_number
)
{
    u32 port_offset =
        xhci_port_offset(controller, port_number);

    u32 portsc = xhci_read32(controller, port_offset);

    if ((portsc & XHCI_PORTSC_CCS) == 0u)
    {
        return false;
    }

    if (
        controller->port_power_control
        &&
        (portsc & XHCI_PORTSC_PP) == 0u
    )
    {
        xhci_write32(
            controller,
            port_offset,
            xhci_port_write_base(portsc)
            |
            XHCI_PORTSC_PP
        );

        portsc = xhci_read32(controller, port_offset);
    }

    u8 protocol_major =
        controller->port_protocol_major[port_number - 1u];

    if ((portsc & XHCI_PORTSC_PED) == 0u)
    {
        u32 reset_bit =
            protocol_major >= 3u
            ?
            XHCI_PORTSC_WPR
            :
            XHCI_PORTSC_PR;

        xhci_write32(
            controller,
            port_offset,
            xhci_port_write_base(portsc)
            |
            reset_bit
        );

        bool reset_finished =
            xhci_wait_bits(
                controller,
                port_offset,
                reset_bit,
                0u
            );

        if (!reset_finished)
        {
            return false;
        }

        portsc = xhci_read32(controller, port_offset);
    }

    if (
        (portsc & XHCI_PORTSC_CCS) == 0u
        ||
        (portsc & XHCI_PORTSC_PED) == 0u
    )
    {
        return false;
    }

    u8 speed_id =
        (u8)(
            (portsc & XHCI_PORTSC_SPEED_MASK)
            >>
            XHCI_PORTSC_SPEED_SHIFT
        );

    if (speed_id == 0u)
    {
        return false;
    }

    u8 slot_id = 0u;

    if (!xhci_enable_slot(controller, &slot_id))
    {
        return false;
    }

    int runtime_index = xhci_alloc_runtime_device();

    if (runtime_index < 0)
    {
        xhci_disable_slot(controller, slot_id);
        return false;
    }

    if (
        !xhci_address_device(
            controller,
            (usize)runtime_index,
            port_number,
            speed_id,
            slot_id
        )
    )
    {
        xhci_disable_slot(controller, slot_id);
        xhci_free_runtime_device((usize)runtime_index);
        return false;
    }

    const u8* device_descriptor = NULL;

    /*
     * First read eight bytes.  This is enough to validate the descriptor and
     * inspect bMaxPacketSize0 on full-speed devices.
     */
    if (
        !xhci_get_descriptor(
            controller,
            (usize)runtime_index,
            USB_DESCRIPTOR_DEVICE,
            0u,
            0u,
            8u,
            &device_descriptor
        )
    )
    {
        xhci_disable_slot(controller, slot_id);
        xhci_free_runtime_device((usize)runtime_index);
        return false;
    }

    if (
        device_descriptor[0] < 8u
        ||
        device_descriptor[1] != USB_DESCRIPTOR_DEVICE
    )
    {
        xhci_disable_slot(controller, slot_id);
        xhci_free_runtime_device((usize)runtime_index);
        return false;
    }

    /*
     * Low/full-speed devices normally begin at 8 bytes.  If a full-speed
     * device advertises another valid EP0 size, update the context before the
     * complete descriptor request.
     */
    if (
        speed_id == 1u
        &&
        (
            device_descriptor[7] == 8u
            ||
            device_descriptor[7] == 16u
            ||
            device_descriptor[7] == 32u
            ||
            device_descriptor[7] == 64u
        )
        &&
        device_descriptor[7]
        !=
        runtime_devices[runtime_index].endpoint0_max_packet
    )
    {
        u8 context_size = controller->context_size;

        root_memzero(
            input_contexts[runtime_index],
            XHCI_INPUT_CONTEXT_BYTES
        );

        u32* input_control =
            (u32*)xhci_input_context(
                (usize)runtime_index,
                context_size,
                0u
            );

        input_control[1] = (1u << 1);

        u32* endpoint0 =
            (u32*)xhci_input_context(
                (usize)runtime_index,
                context_size,
                2u
            );

        u16 new_packet = device_descriptor[7];

        endpoint0[1] =
            (3u << 1)
            |
            (XHCI_ENDPOINT_TYPE_CONTROL << 3)
            |
            ((u32)new_packet << 16);

        u64 transfer_ring_address =
            xhci_physical(
                &transfer_rings[runtime_index][0]
            );

        endpoint0[2] =
            (u32)(transfer_ring_address & 0xFFFFFFFFu)
            |
            1u;
        endpoint0[3] = (u32)(transfer_ring_address >> 32);
        endpoint0[4] = 8u;

        XhciTrb completion;

        if (
            xhci_command(
                controller,
                xhci_physical(
                    &input_contexts[runtime_index][0]
                ),
                0u,
                (XHCI_TRB_TYPE_EVALUATE_CONTEXT << XHCI_TRB_TYPE_SHIFT)
                |
                ((u32)slot_id << 24),
                &completion
            )
            &&
            xhci_completion_code(&completion) == XHCI_COMPLETION_SUCCESS
        )
        {
            runtime_devices[runtime_index].endpoint0_max_packet =
                new_packet;
        }
    }

    if (
        !xhci_get_descriptor(
            controller,
            (usize)runtime_index,
            USB_DESCRIPTOR_DEVICE,
            0u,
            0u,
            18u,
            &device_descriptor
        )
    )
    {
        xhci_disable_slot(controller, slot_id);
        xhci_free_runtime_device((usize)runtime_index);
        return false;
    }

    if (
        device_descriptor[0] < 18u
        ||
        device_descriptor[1] != USB_DESCRIPTOR_DEVICE
    )
    {
        xhci_disable_slot(controller, slot_id);
        xhci_free_runtime_device((usize)runtime_index);
        return false;
    }

    UsbDeviceInfo device;
    root_memzero(&device, sizeof(device));

    device.controller_index = controller->index;
    device.port_number = port_number;
    device.slot_id = slot_id;
    device.speed_id = speed_id;
    device.speed = xhci_usb_speed(speed_id);

    u32* device_slot_context =
        (u32*)xhci_device_context(
            (usize)runtime_index,
            controller->context_size,
            0u
        );

    device.address =
        (u8)(device_slot_context[3] & 0xFFu);

    device.usb_version_bcd =
        (u16)device_descriptor[2]
        |
        ((u16)device_descriptor[3] << 8);

    device.device_class = device_descriptor[4];
    device.device_subclass = device_descriptor[5];
    device.device_protocol = device_descriptor[6];
    device.endpoint0_max_packet =
        runtime_devices[runtime_index].endpoint0_max_packet;

    device.vendor_id =
        (u16)device_descriptor[8]
        |
        ((u16)device_descriptor[9] << 8);

    device.product_id =
        (u16)device_descriptor[10]
        |
        ((u16)device_descriptor[11] << 8);

    device.device_version_bcd =
        (u16)device_descriptor[12]
        |
        ((u16)device_descriptor[13] << 8);

    u8 manufacturer_index = device_descriptor[14];
    u8 product_index = device_descriptor[15];
    u8 serial_index = device_descriptor[16];
    device.configuration_count = device_descriptor[17];

    u16 language_id =
        xhci_read_language_id(
            controller,
            (usize)runtime_index
        );

    xhci_read_string(
        controller,
        (usize)runtime_index,
        manufacturer_index,
        language_id,
        device.manufacturer,
        sizeof(device.manufacturer)
    );

    xhci_read_string(
        controller,
        (usize)runtime_index,
        product_index,
        language_id,
        device.product,
        sizeof(device.product)
    );

    xhci_read_string(
        controller,
        (usize)runtime_index,
        serial_index,
        language_id,
        device.serial,
        sizeof(device.serial)
    );

    if (device.configuration_count > 0u)
    {
        xhci_parse_configuration(
            controller,
            (usize)runtime_index,
            &device
        );
    }

    u32 usb_id = 0u;

    if (!usb_register_device(&device, &usb_id))
    {
        xhci_disable_slot(controller, slot_id);
        xhci_free_runtime_device((usize)runtime_index);
        return false;
    }

    runtime_devices[runtime_index].usb_device_id = usb_id;

    controller->port_slot[port_number - 1u] = slot_id;
    controller->port_has_usb[port_number - 1u] = true;
    controller->port_usb_id[port_number - 1u] = usb_id;

    return true;
}

static void xhci_scan_ports(XhciController* controller)
{
    for (u16 port = 1u; port <= controller->max_ports; port++)
    {
        u8 port_number = (u8)port;

        controller->port_slot[port_number - 1u] = 0u;
        controller->port_has_usb[port_number - 1u] = false;
        controller->port_usb_id[port_number - 1u] = 0u;

        (void)xhci_enumerate_port(
            controller,
            port_number
        );
    }
}

static int xhci_find_runtime_by_port(
    u8 controller_index,
    u8 port_number
)
{
    for (usize i = 0u; i < XHCI_MAX_TRACKED_DEVICES; i++)
    {
        if (
            runtime_devices[i].used
            && runtime_devices[i].controller_index == controller_index
            && runtime_devices[i].port_number == port_number
        )
        {
            return (int)i;
        }
    }

    return -1;
}

static void xhci_detach_port(
    XhciController* controller,
    u8 port_number
)
{
    if (controller == NULL || port_number == 0u || port_number > controller->max_ports)
        return;

    usize port_index = (usize)(port_number - 1u);

    if (controller->port_has_usb[port_index])
    {
        (void)usb_remove_device(controller->port_usb_id[port_index]);
        controller->disconnect_events++;
    }

    int runtime_index = xhci_find_runtime_by_port(controller->index, port_number);
    if (runtime_index >= 0)
    {
        u8 slot_id = runtime_devices[runtime_index].slot_id;
        if (controller->running && slot_id != 0u)
            xhci_disable_slot(controller, slot_id);
        xhci_free_runtime_device((usize)runtime_index);
    }

    controller->port_slot[port_index] = 0u;
    controller->port_has_usb[port_index] = false;
    controller->port_usb_id[port_index] = 0u;
}

static void xhci_schedule_port_reprobe(
    XhciController* controller,
    u8 port_number,
    u32 delay_ms
)
{
    if (controller == NULL || port_number == 0u || port_number > controller->max_ports)
        return;

    usize port_index = (usize)(port_number - 1u);
    controller->port_reprobe_pending[port_index] = true;
    controller->port_reprobe_at_ms[port_index] = root_time_millis() + delay_ms;
}

static bool xhci_controller_stop(XhciController* controller)
{
    if (!controller->used)
    {
        return true;
    }

    u32 command =
        xhci_read32(
            controller,
            xhci_operational(controller, XHCI_OP_USBCMD)
        );

    command &= ~XHCI_USBCMD_RUN;

    xhci_write32(
        controller,
        xhci_operational(controller, XHCI_OP_USBCMD),
        command
    );

    bool stopped =
        xhci_wait_bits(
            controller,
            xhci_operational(controller, XHCI_OP_USBSTS),
            XHCI_USBSTS_HCH,
            XHCI_USBSTS_HCH
        );

    controller->running = false;
    return stopped;
}

static bool xhci_controller_setup(
    XhciController* controller,
    const PciDevice* pci_device
)
{
    root_memzero(controller, sizeof(*controller));

    if (
        pci_device == NULL
        ||
        pci_device->bar_count == 0u
        ||
        !pci_device->bars[0].present
        ||
        (
            pci_device->bars[0].type != PCI_BAR_MEMORY32
            &&
            pci_device->bars[0].type != PCI_BAR_MEMORY64
        )
        ||
        pci_device->bars[0].base == 0u
        ||
        pci_device->bars[0].base > 0xFFFFFFFFull
    )
    {
        return false;
    }

    usize controller_index =
        (usize)(controller - controllers);

    controller->index = (u8)controller_index;
    controller->pci = *pci_device;
    controller->mmio_base = (u32)pci_device->bars[0].base;
    controller->used = true;
    controller->init_attempted = true;
    controller->init_failed = false;
    controller->last_error = XHCI_ERROR_NONE;

    if (
        !pci_enable_memory(pci_device->address)
        ||
        !pci_enable_bus_mastering(pci_device->address)
    )
    {
        controller->init_failed = true;
        controller->last_error = XHCI_ERROR_PCI_ENABLE;
        return false;
    }

    controller->cap_length =
        xhci_read8(controller, XHCI_CAP_CAPLENGTH);

    controller->hci_version =
        xhci_read16(controller, XHCI_CAP_HCIVERSION);

    u32 hcsparams1 =
        xhci_read32(controller, XHCI_CAP_HCSPARAMS1);

    u32 hcsparams2 =
        xhci_read32(controller, XHCI_CAP_HCSPARAMS2);

    u32 hccparams1 =
        xhci_read32(controller, XHCI_CAP_HCCPARAMS1);

    controller->max_slots =
        (u8)(hcsparams1 & 0xFFu);

    controller->max_ports =
        (u8)((hcsparams1 >> 24) & 0xFFu);

    controller->port_power_control =
        (hccparams1 & (1u << 3)) != 0u;

    controller->context_size =
        (hccparams1 & (1u << 2)) != 0u
        ?
        64u
        :
        32u;

    controller->scratchpad_count =
        xhci_max_scratchpads(hcsparams2);

    if (
        controller->max_slots == 0u
        ||
        controller->max_ports == 0u
        ||
        controller->scratchpad_count > XHCI_MAX_SCRATCHPADS
    )
    {
        controller->init_failed = true;
        controller->last_error = XHCI_ERROR_BAD_CAPS;
        return false;
    }

    controller->enabled_slots =
        controller->max_slots > XHCI_MAX_TRACKED_DEVICES
        ?
        XHCI_MAX_TRACKED_DEVICES
        :
        controller->max_slots;

    controller->operational_offset = controller->cap_length;
    controller->doorbell_offset =
        xhci_read32(controller, XHCI_CAP_DBOFF)
        &
        ~0x03u;

    controller->runtime_offset =
        xhci_read32(controller, XHCI_CAP_RTSOFF)
        &
        ~0x1Fu;

    xhci_parse_extended_capabilities(
        controller,
        hccparams1
    );

    /* Stop before reset. */
    (void)xhci_controller_stop(controller);

    u32 command =
        xhci_read32(
            controller,
            xhci_operational(controller, XHCI_OP_USBCMD)
        );

    xhci_write32(
        controller,
        xhci_operational(controller, XHCI_OP_USBCMD),
        command | XHCI_USBCMD_HCRST
    );

    if (
        !xhci_wait_bits(
            controller,
            xhci_operational(controller, XHCI_OP_USBCMD),
            XHCI_USBCMD_HCRST,
            0u
        )
        ||
        !xhci_wait_bits(
            controller,
            xhci_operational(controller, XHCI_OP_USBSTS),
            XHCI_USBSTS_CNR,
            0u
        )
    )
    {
        controller->init_failed = true;
        controller->last_error = XHCI_ERROR_RESET_TIMEOUT;
        return false;
    }

    u32 pagesize =
        xhci_read32(
            controller,
            xhci_operational(controller, XHCI_OP_PAGESIZE)
        );

    if ((pagesize & 1u) == 0u)
    {
        controller->init_failed = true;
        controller->last_error = XHCI_ERROR_PAGE_SIZE;
        return false;
    }

    root_memzero(
        dcbaa[controller_index],
        sizeof(dcbaa[controller_index])
    );

    root_memzero(
        command_rings[controller_index],
        sizeof(command_rings[controller_index])
    );

    root_memzero(
        event_rings[controller_index],
        sizeof(event_rings[controller_index])
    );

    root_memzero(
        scratchpad_pointers[controller_index],
        sizeof(scratchpad_pointers[controller_index])
    );

    if (controller->scratchpad_count > 0u)
    {
        for (
            u16 i = 0u;
            i < controller->scratchpad_count;
            i++
        )
        {
            root_memzero(
                scratchpad_buffers[controller_index][i],
                4096u
            );

            scratchpad_pointers[controller_index][i] =
                xhci_physical(
                    scratchpad_buffers[controller_index][i]
                );
        }

        dcbaa[controller_index][0] =
            xhci_physical(
                scratchpad_pointers[controller_index]
            );
    }

    controller->command_enqueue = 0u;
    controller->event_dequeue = 0u;
    controller->command_cycle = 1u;
    controller->event_cycle = 1u;

    xhci_prepare_command_ring(controller);

    u64 command_ring_address =
        xhci_physical(
            &command_rings[controller_index][0]
        );

    xhci_write64(
        controller,
        xhci_operational(controller, XHCI_OP_CRCR),
        command_ring_address | 1ull
    );

    xhci_write64(
        controller,
        xhci_operational(controller, XHCI_OP_DCBAAP),
        xhci_physical(
            dcbaa[controller_index]
        )
    );

    erst_entries[controller_index].ring_segment_base =
        xhci_physical(
            &event_rings[controller_index][0]
        );
    erst_entries[controller_index].ring_segment_size =
        XHCI_EVENT_TRBS;
    erst_entries[controller_index].reserved = 0u;

    u32 interrupter =
        controller->runtime_offset
        +
        0x20u;

    xhci_write32(
        controller,
        interrupter + 0x08u,
        1u
    );

    xhci_write64(
        controller,
        interrupter + 0x10u,
        xhci_physical(
            &erst_entries[controller_index]
        )
    );

    xhci_write64(
        controller,
        interrupter + 0x18u,
        xhci_physical(
            &event_rings[controller_index][0]
        )
    );

    xhci_write32(
        controller,
        xhci_operational(controller, XHCI_OP_CONFIG),
        controller->enabled_slots
    );

    xhci_barrier();

    command =
        xhci_read32(
            controller,
            xhci_operational(controller, XHCI_OP_USBCMD)
        );

    command |= XHCI_USBCMD_RUN;

    xhci_write32(
        controller,
        xhci_operational(controller, XHCI_OP_USBCMD),
        command
    );

    if (
        !xhci_wait_bits(
            controller,
            xhci_operational(controller, XHCI_OP_USBSTS),
            XHCI_USBSTS_HCH,
            0u
        )
    )
    {
        controller->init_failed = true;
        controller->last_error = XHCI_ERROR_START_TIMEOUT;
        return false;
    }

    controller->running = true;
    controller->init_failed = false;
    controller->last_error = XHCI_ERROR_NONE;
    xhci_scan_ports(controller);

    return true;
}

static bool xhci_driver_probe(const RootDevice* device)
{
    if (
        device == NULL
        ||
        device->bus != ROOT_DEVICE_BUS_PCI
    )
    {
        return false;
    }

    const PciDevice* pci = &device->pci;

    return
        pci->class_code == PCI_CLASS_SERIAL_BUS
        &&
        pci->subclass == PCI_SUBCLASS_USB
        &&
        pci->prog_if == PCI_PROGIF_XHCI
        &&
        pci->bar_count > 0u
        &&
        pci->bars[0].present
        &&
        pci->bars[0].base <= 0xFFFFFFFFull;
}

static int xhci_find_controller_by_pci(PciAddress address)
{
    for (usize i = 0u; i < XHCI_MAX_CONTROLLERS; i++)
    {
        if (
            controllers[i].used
            &&
            controllers[i].pci.address.bus == address.bus
            &&
            controllers[i].pci.address.device == address.device
            &&
            controllers[i].pci.address.function == address.function
        )
        {
            return (int)i;
        }
    }

    return -1;
}

static bool xhci_driver_attach(RootDevice* device)
{
    if (!xhci_driver_probe(device))
    {
        return false;
    }

    if (xhci_find_controller_by_pci(device->pci.address) >= 0)
    {
        return true;
    }

    for (usize i = 0u; i < XHCI_MAX_CONTROLLERS; i++)
    {
        if (controllers[i].used)
        {
            continue;
        }

        /* Discovery only. Never reset/enumerate xHCI during kernel boot. */
        root_memzero(&controllers[i], sizeof(controllers[i]));
        controllers[i].used = true;
        controllers[i].index = (u8)i;
        controllers[i].pci = device->pci;
        controllers[i].mmio_base = (u32)device->pci.bars[0].base;
        controllers[i].last_error = XHCI_ERROR_NONE;

        active_controller_count++;
        return true;
    }

    return false;
}

static void xhci_driver_detach(RootDevice* device)
{
    if (device == NULL)
    {
        return;
    }

    int index =
        xhci_find_controller_by_pci(
            device->pci.address
        );

    if (index < 0)
    {
        return;
    }

    if (controllers[index].running)
    {
        (void)xhci_controller_stop(
            &controllers[index]
        );
    }

    root_memzero(
        &controllers[index],
        sizeof(controllers[index])
    );

    if (active_controller_count > 0u)
    {
        active_controller_count--;
    }

    if (active_controller_count == 0u)
    {
        root_memzero(
            runtime_devices,
            sizeof(runtime_devices)
        );

        usb_reset_inventory();
    }
}

static const RootDriver xhci_root_driver =
{
    .name = "xhci",
    .match =
    {
        .vendor_id = ROOT_DRIVER_ANY_VENDOR,
        .device_id = ROOT_DRIVER_ANY_DEVICE,
        .class_code = PCI_CLASS_SERIAL_BUS,
        .subclass = PCI_SUBCLASS_USB,
        .prog_if = PCI_PROGIF_XHCI
    },
    .probe = xhci_driver_probe,
    .attach = xhci_driver_attach,
    .detach = xhci_driver_detach
};

void xhci_init(void)
{
    root_memzero(controllers, sizeof(controllers));
    root_memzero(runtime_devices, sizeof(runtime_devices));
    active_controller_count = 0u;

    (void)device_manager_register_driver(
        &xhci_root_driver
    );

    device_manager_bind_drivers();
}

static bool xhci_start_controller_slot(usize slot)
{
    if (slot >= XHCI_MAX_CONTROLLERS || !controllers[slot].used)
    {
        return false;
    }

    if (controllers[slot].running)
    {
        return true;
    }

    PciDevice pci = controllers[slot].pci;

    if (xhci_controller_setup(&controllers[slot], &pci))
    {
        return true;
    }

    u8 error = controllers[slot].last_error;

    /* Keep the detected PCI controller visible after a failed start. */
    root_memzero(&controllers[slot], sizeof(controllers[slot]));
    controllers[slot].used = true;
    controllers[slot].index = (u8)slot;
    controllers[slot].pci = pci;
    controllers[slot].mmio_base = (u32)pci.bars[0].base;
    controllers[slot].init_attempted = true;
    controllers[slot].init_failed = true;
    controllers[slot].last_error =
        error == XHCI_ERROR_NONE
        ? XHCI_ERROR_NOT_READY
        : error;

    return false;
}

bool xhci_start(void)
{
    if (active_controller_count == 0u)
    {
        return false;
    }

    if (xhci_any_running())
    {
        return true;
    }

    usb_reset_inventory();
    root_memzero(runtime_devices, sizeof(runtime_devices));

    bool any_running = false;
    bool all_ok = true;

    for (usize i = 0u; i < XHCI_MAX_CONTROLLERS; i++)
    {
        if (!controllers[i].used)
        {
            continue;
        }

        if (!xhci_start_controller_slot(i))
        {
            all_ok = false;
            continue;
        }

        any_running = true;
    }

    return any_running && all_ok;
}

bool xhci_rescan(void)
{
    for (usize i = 0u; i < XHCI_MAX_CONTROLLERS; i++)
    {
        if (controllers[i].used && controllers[i].running)
        {
            (void)xhci_controller_stop(&controllers[i]);
        }
    }

    usb_reset_inventory();
    root_memzero(runtime_devices, sizeof(runtime_devices));

    bool any_running = false;
    bool all_ok = true;

    for (usize i = 0u; i < XHCI_MAX_CONTROLLERS; i++)
    {
        if (!controllers[i].used)
        {
            continue;
        }

        if (!xhci_start_controller_slot(i))
        {
            all_ok = false;
            continue;
        }

        any_running = true;
    }

    return any_running && all_ok;
}

void xhci_poll(void)
{
    u64 now = root_time_millis();

    for (usize i = 0u; i < XHCI_MAX_CONTROLLERS; i++)
    {
        XhciController* controller = &controllers[i];
        if (!controller->used || !controller->running)
            continue;

        /* Collect persistent class-driver transfers first. PORTSC below remains
         * authoritative for root-port hotplug state. */
        xhci_collect_idle_events(controller, 64u);

        for (u16 port = 1u; port <= controller->max_ports; port++)
        {
            u8 port_number = (u8)port;
            usize port_index = (usize)(port_number - 1u);
            u32 offset = xhci_port_offset(controller, port_number);
            u32 portsc = xhci_read32(controller, offset);
            u32 changes = portsc & XHCI_PORTSC_CHANGE_BITS;

            if (changes != 0u)
            {
                /* PORTSC change fields are RW1C. Preserve normal status/control
                 * bits and write ones only for change flags being acknowledged. */
                xhci_write32(
                    controller,
                    offset,
                    xhci_port_write_base(portsc) | changes
                );

                if ((changes & XHCI_PORTSC_CSC) != 0u)
                {
                    if ((portsc & XHCI_PORTSC_CCS) == 0u)
                    {
                        xhci_detach_port(controller, port_number);
                        controller->port_reprobe_pending[port_index] = false;
                        controller->port_reprobe_attempts[port_index] = 0u;
                    }
                    else
                    {
                        /* USB requires connection debounce before reset/address. */
                        controller->port_reprobe_attempts[port_index] = 0u;
                        xhci_schedule_port_reprobe(controller, port_number, 100u);
                    }
                }

                /* A connected port that loses enable or reports a configure
                 * error is recovered as a device detach/re-enumerate, rather
                 * than leaving class drivers bound to dead endpoint contexts. */
                if (
                    (portsc & XHCI_PORTSC_CCS) != 0u
                    && (portsc & XHCI_PORTSC_PED) == 0u
                    && (changes & (XHCI_PORTSC_PEC | XHCI_PORTSC_CEC)) != 0u
                )
                {
                    xhci_detach_port(controller, port_number);
                    controller->port_reprobe_attempts[port_index] = 0u;
                    xhci_schedule_port_reprobe(controller, port_number, 100u);
                }
            }

            if (
                controller->port_reprobe_pending[port_index]
                && now >= controller->port_reprobe_at_ms[port_index]
            )
            {
                portsc = xhci_read32(controller, offset);

                if ((portsc & XHCI_PORTSC_CCS) == 0u)
                {
                    controller->port_reprobe_pending[port_index] = false;
                    controller->port_reprobe_attempts[port_index] = 0u;
                    continue;
                }

                if (controller->port_has_usb[port_index])
                {
                    controller->port_reprobe_pending[port_index] = false;
                    controller->port_reprobe_attempts[port_index] = 0u;
                    continue;
                }

                if (xhci_enumerate_port(controller, port_number))
                {
                    controller->port_reprobe_pending[port_index] = false;
                    controller->port_reprobe_attempts[port_index] = 0u;
                }
                else
                {
                    controller->port_reprobe_attempts[port_index]++;
                    if (controller->port_reprobe_attempts[port_index] >= 3u)
                    {
                        controller->port_reprobe_pending[port_index] = false;
                    }
                    else
                    {
                        controller->port_reprobe_at_ms[port_index] = now + 250u;
                    }
                }
            }
        }
    }
}

bool xhci_any_running(void)
{
    for (usize i = 0u; i < XHCI_MAX_CONTROLLERS; i++)
    {
        if (controllers[i].used && controllers[i].running)
        {
            return true;
        }
    }

    return false;
}

usize xhci_controller_count(void)
{
    return active_controller_count;
}

bool xhci_get_controller_info(
    usize index,
    XhciControllerInfo* output
)
{
    if (output == NULL)
    {
        return false;
    }

    usize seen = 0u;

    for (usize i = 0u; i < XHCI_MAX_CONTROLLERS; i++)
    {
        XhciController* controller = &controllers[i];

        if (!controller->used)
        {
            continue;
        }

        if (seen != index)
        {
            seen++;
            continue;
        }

        root_memzero(output, sizeof(*output));

        output->index = controller->index;
        output->active = controller->running;
        output->init_attempted = controller->init_attempted;
        output->init_failed = controller->init_failed;
        output->last_error = controller->last_error;
        output->pci_address = controller->pci.address;
        output->mmio_base = controller->mmio_base;
        output->hci_version = controller->hci_version;
        output->max_slots = controller->max_slots;
        output->enabled_slots = controller->enabled_slots;
        output->max_ports = controller->max_ports;
        output->context_size = controller->context_size;
        output->scratchpad_count = controller->scratchpad_count;
        output->hotplug_events = controller->hotplug_events;
        output->disconnect_events = controller->disconnect_events;
        output->endpoint_recoveries = controller->endpoint_recoveries;
        output->transfer_errors = controller->transfer_errors;

        usize device_count = 0u;

        for (u16 port = 0u; port < controller->max_ports; port++)
        {
            if (controller->port_has_usb[port])
            {
                device_count++;
            }
        }

        output->enumerated_devices = device_count;
        return true;
    }

    return false;
}

usize xhci_port_count(void)
{
    usize count = 0u;

    for (usize i = 0u; i < XHCI_MAX_CONTROLLERS; i++)
    {
        if (controllers[i].used && controllers[i].running)
        {
            count += controllers[i].max_ports;
        }
    }

    return count;
}

bool xhci_get_port_info(
    usize global_index,
    XhciPortInfo* output
)
{
    if (output == NULL)
    {
        return false;
    }

    usize cursor = 0u;

    for (usize i = 0u; i < XHCI_MAX_CONTROLLERS; i++)
    {
        XhciController* controller = &controllers[i];

        if (!controller->used || !controller->running)
        {
            continue;
        }

        for (u16 port = 1u; port <= controller->max_ports; port++)
        {
            u8 port_number = (u8)port;

            if (cursor++ != global_index)
            {
                continue;
            }

            u32 portsc =
                xhci_read32(
                    controller,
                    xhci_port_offset(controller, port_number)
                );

            root_memzero(output, sizeof(*output));

            output->controller_index = controller->index;
            output->port_number = port_number;
            output->protocol_major =
                controller->port_protocol_major[port_number - 1u];
            output->protocol_minor =
                controller->port_protocol_minor[port_number - 1u];
            output->connected =
                (portsc & XHCI_PORTSC_CCS) != 0u;
            output->enabled =
                (portsc & XHCI_PORTSC_PED) != 0u;
            output->powered =
                !controller->port_power_control
                ||
                (portsc & XHCI_PORTSC_PP) != 0u;
            output->speed_id =
                (u8)(
                    (portsc & XHCI_PORTSC_SPEED_MASK)
                    >>
                    XHCI_PORTSC_SPEED_SHIFT
                );
            output->link_state =
                (u8)(
                    (portsc & XHCI_PORTSC_PLS_MASK)
                    >>
                    XHCI_PORTSC_PLS_SHIFT
                );
            output->slot_id =
                controller->port_slot[port_number - 1u];
            output->has_usb_device =
                controller->port_has_usb[port_number - 1u];
            output->usb_device_id =
                controller->port_usb_id[port_number - 1u];

            return true;
        }
    }

    return false;
}

bool xhci_control_transfer(
    u32 usb_device_id,
    u8 request_type,
    u8 request,
    u16 value,
    u16 index,
    void* data,
    u16 length,
    usize* actual_length
)
{
    if (actual_length != NULL)
    {
        *actual_length = 0u;
    }

    int runtime_index_signed = xhci_find_runtime_by_usb_id(usb_device_id);

    if (
        runtime_index_signed < 0
        || (length > 0u && data == NULL)
        || length > XHCI_CONTROL_BUFFER_SIZE
    )
    {
        return false;
    }

    usize runtime_index = (usize)runtime_index_signed;
    XhciDeviceRuntime* runtime = &runtime_devices[runtime_index];

    if (runtime->controller_index >= XHCI_MAX_CONTROLLERS)
    {
        return false;
    }

    XhciController* controller = &controllers[runtime->controller_index];

    if (!controller->used || !controller->running)
    {
        return false;
    }

    if (length == 0u)
    {
        bool ok = xhci_control_no_data(
            controller,
            runtime_index,
            request_type,
            request,
            value,
            index
        );

        if (ok && actual_length != NULL)
        {
            *actual_length = 0u;
        }

        return ok;
    }

    if (!xhci_control_ensure_space(runtime_index, 3u))
        return false;

    bool direction_in = (request_type & 0x80u) != 0u;
    u8* buffer = control_buffers[runtime_index];

    root_memzero(buffer, XHCI_CONTROL_BUFFER_SIZE);

    if (!direction_in)
    {
        root_memcpy(buffer, data, length);
    }

    u64 setup =
        (u64)request_type
        | ((u64)request << 8)
        | ((u64)value << 16)
        | ((u64)index << 32)
        | ((u64)length << 48);

    u32 setup_control =
        (XHCI_TRB_TYPE_SETUP_STAGE << XHCI_TRB_TYPE_SHIFT)
        | XHCI_TRB_IDT
        | XHCI_TRB_CHAIN
        | ((direction_in ? 3u : 2u) << 16);

    if (
        !xhci_transfer_enqueue(
            runtime_index,
            setup,
            8u,
            setup_control
        )
    )
    {
        return false;
    }

    u32 data_control =
        (XHCI_TRB_TYPE_DATA_STAGE << XHCI_TRB_TYPE_SHIFT)
        | XHCI_TRB_CHAIN;

    if (direction_in)
    {
        data_control |= XHCI_TRB_DIR_IN;
    }

    if (
        !xhci_transfer_enqueue(
            runtime_index,
            xhci_physical(buffer),
            length,
            data_control
        )
    )
    {
        return false;
    }

    u32 status_control =
        (XHCI_TRB_TYPE_STATUS_STAGE << XHCI_TRB_TYPE_SHIFT)
        | XHCI_TRB_IOC;

    /* Status stage uses the opposite direction from the data stage. */
    if (!direction_in)
    {
        status_control |= XHCI_TRB_DIR_IN;
    }

    if (
        !xhci_transfer_enqueue(
            runtime_index,
            0u,
            0u,
            status_control
        )
    )
    {
        return false;
    }

    xhci_barrier();

    xhci_write32(
        controller,
        controller->doorbell_offset + ((u32)runtime->slot_id * 4u),
        1u
    );

    XhciTrb completion;

    if (
        !xhci_wait_transfer_completion(
            controller,
            runtime->slot_id,
            1u,
            &completion
        )
    )
    {
        return false;
    }

    u8 code = xhci_completion_code(&completion);

    if (
        code != XHCI_COMPLETION_SUCCESS
        && code != XHCI_COMPLETION_SHORT_PACKET
    )
    {
        return false;
    }

    if (direction_in)
    {
        /* The completion event belongs to the Status Stage, so it does not
         * carry the Data Stage residual length. RNDIS messages contain their
         * own length and are bounds-checked by the class driver. */
        root_memcpy(data, buffer, length);
    }

    if (actual_length != NULL)
    {
        *actual_length = length;
    }

    return true;
}

static u8 xhci_endpoint_max_burst(
    u32 usb_device_id,
    u8 endpoint_address
)
{
    UsbDeviceInfo usb;
    if (!usb_get_device_by_id(usb_device_id, &usb))
        return 0u;

    for (u8 i = 0u; i < usb.interface_count; i++)
    {
        const UsbInterfaceInfo* interface = &usb.interfaces[i];
        for (u8 e = 0u; e < interface->endpoint_count; e++)
        {
            const UsbEndpointInfo* endpoint = &interface->endpoints[e];
            if (endpoint->address == endpoint_address)
                return endpoint->max_burst;
        }
    }

    return 0u;
}

bool xhci_configure_bulk_interface(
    u32 usb_device_id,
    u8 configuration_value,
    u8 interface_number,
    u8 alternate_setting,
    u8 bulk_out_endpoint,
    u16 bulk_out_packet_size,
    u8 bulk_in_endpoint,
    u16 bulk_in_packet_size
)
{
    int runtime_index_signed = xhci_find_runtime_by_usb_id(usb_device_id);

    if (
        runtime_index_signed < 0
        || bulk_out_packet_size == 0u
        || bulk_in_packet_size == 0u
        || (bulk_out_endpoint & 0x80u) != 0u
        || (bulk_in_endpoint & 0x80u) == 0u
    )
    {
        return false;
    }

    usize runtime_index = (usize)runtime_index_signed;
    XhciDeviceRuntime* runtime = &runtime_devices[runtime_index];

    if (runtime->controller_index >= XHCI_MAX_CONTROLLERS)
    {
        return false;
    }

    XhciController* controller = &controllers[runtime->controller_index];

    if (!controller->used || !controller->running)
    {
        return false;
    }

    u8 out_dci = xhci_endpoint_dci(bulk_out_endpoint);
    u8 in_dci = xhci_endpoint_dci(bulk_in_endpoint);

    if (
        out_dci <= 1u
        || in_dci <= 1u
        || out_dci >= 32u
        || in_dci >= 32u
        || out_dci == in_dci
    )
    {
        return false;
    }

    if (
        !xhci_control_no_data(
            controller,
            runtime_index,
            0x00u,
            USB_REQUEST_SET_CONFIGURATION,
            configuration_value,
            0u
        )
    )
    {
        return false;
    }

    if (
        alternate_setting != 0u
        && !xhci_control_no_data(
            controller,
            runtime_index,
            0x01u,
            USB_REQUEST_SET_INTERFACE,
            alternate_setting,
            interface_number
        )
    )
    {
        return false;
    }

    root_memzero(
        input_contexts[runtime_index],
        XHCI_INPUT_CONTEXT_BYTES
    );

    xhci_prepare_normal_transfer_ring(
        &bulk_out_rings[runtime_index][0]
    );

    xhci_prepare_normal_transfer_ring(
        &bulk_in_rings[runtime_index][0]
    );

    u8 context_size = controller->context_size;

    u32* input_control =
        (u32*)xhci_input_context(
            runtime_index,
            context_size,
            0u
        );

    input_control[1] =
        (1u << 0)
        |
        (1u << out_dci)
        |
        (1u << in_dci);

    u32* input_slot =
        (u32*)xhci_input_context(
            runtime_index,
            context_size,
            1u
        );

    u32* current_slot =
        (u32*)xhci_device_context(
            runtime_index,
            context_size,
            0u
        );

    root_memcpy(input_slot, current_slot, context_size);

    u8 highest_dci = out_dci > in_dci ? out_dci : in_dci;
    input_slot[0] &= ~(0x1Fu << 27);
    input_slot[0] |= (u32)highest_dci << 27;

    u32* out_context =
        (u32*)xhci_input_context(
            runtime_index,
            context_size,
            (u8)(out_dci + 1u)
        );

    xhci_prepare_bulk_endpoint_context(
        out_context,
        XHCI_ENDPOINT_TYPE_BULK_OUT,
        bulk_out_packet_size,
        xhci_endpoint_max_burst(usb_device_id, bulk_out_endpoint),
        xhci_physical(&bulk_out_rings[runtime_index][0])
    );

    u32* in_context =
        (u32*)xhci_input_context(
            runtime_index,
            context_size,
            (u8)(in_dci + 1u)
        );

    xhci_prepare_bulk_endpoint_context(
        in_context,
        XHCI_ENDPOINT_TYPE_BULK_IN,
        bulk_in_packet_size,
        xhci_endpoint_max_burst(usb_device_id, bulk_in_endpoint),
        xhci_physical(&bulk_in_rings[runtime_index][0])
    );

    xhci_barrier();

    XhciTrb completion;

    if (
        !xhci_command(
            controller,
            xhci_physical(&input_contexts[runtime_index][0]),
            0u,
            (XHCI_TRB_TYPE_CONFIGURE_ENDPOINT << XHCI_TRB_TYPE_SHIFT)
            |
            ((u32)runtime->slot_id << 24),
            &completion
        )
        ||
        xhci_completion_code(&completion) != XHCI_COMPLETION_SUCCESS
    )
    {
        return false;
    }

    runtime->bulk_configured = true;
    runtime->bulk_out_endpoint = bulk_out_endpoint;
    runtime->bulk_in_endpoint = bulk_in_endpoint;
    runtime->bulk_out_dci = out_dci;
    runtime->bulk_in_dci = in_dci;
    runtime->bulk_out_enqueue = 0u;
    runtime->bulk_in_enqueue = 0u;
    runtime->bulk_out_cycle = 1u;
    runtime->bulk_in_cycle = 1u;
    runtime->bulk_in_async_pending = false;
    runtime->bulk_in_async_complete = false;
    runtime->bulk_in_async_buffer = 0u;
    runtime->bulk_in_async_length = 0u;

    return true;
}

bool xhci_configure_bulk_endpoints(
    u32 usb_device_id,
    u8 configuration_value,
    u8 bulk_out_endpoint,
    u16 bulk_out_packet_size,
    u8 bulk_in_endpoint,
    u16 bulk_in_packet_size
)
{
    return xhci_configure_bulk_interface(
        usb_device_id,
        configuration_value,
        0u,
        0u,
        bulk_out_endpoint,
        bulk_out_packet_size,
        bulk_in_endpoint,
        bulk_in_packet_size
    );
}

static u8 xhci_bulk_endpoint_state(
    usize runtime_index,
    u8 dci
)
{
    if (runtime_index >= XHCI_MAX_TRACKED_DEVICES)
        return XHCI_ENDPOINT_STATE_DISABLED;

    XhciDeviceRuntime* runtime = &runtime_devices[runtime_index];
    if (!runtime->used || runtime->controller_index >= XHCI_MAX_CONTROLLERS)
        return XHCI_ENDPOINT_STATE_DISABLED;

    XhciController* controller = &controllers[runtime->controller_index];
    u32* endpoint = (u32*)xhci_device_context(
        runtime_index,
        controller->context_size,
        dci
    );

    return (u8)(endpoint[0] & 0x7u);
}


static bool xhci_recover_bulk_endpoint(
    usize runtime_index,
    u8 endpoint_address,
    bool clear_device_halt
)
{
    if (runtime_index >= XHCI_MAX_TRACKED_DEVICES)
        return false;

    XhciDeviceRuntime* runtime = &runtime_devices[runtime_index];
    if (!runtime->used || !runtime->bulk_configured)
        return false;

    if (runtime->controller_index >= XHCI_MAX_CONTROLLERS)
        return false;

    XhciController* controller = &controllers[runtime->controller_index];
    if (!controller->used || !controller->running)
        return false;

    u8 dci;
    XhciTrb* ring;
    u16* enqueue;
    u8* cycle;

    if (endpoint_address == runtime->bulk_out_endpoint)
    {
        dci = runtime->bulk_out_dci;
        ring = &bulk_out_rings[runtime_index][0];
        enqueue = &runtime->bulk_out_enqueue;
        cycle = &runtime->bulk_out_cycle;
    }
    else if (endpoint_address == runtime->bulk_in_endpoint)
    {
        dci = runtime->bulk_in_dci;
        ring = &bulk_in_rings[runtime_index][0];
        enqueue = &runtime->bulk_in_enqueue;
        cycle = &runtime->bulk_in_cycle;
    }
    else
    {
        return false;
    }

    /* A functional STALL halts both the USB device endpoint and xHC endpoint,
     * so clear the device-side halt first. Protocol/transaction errors may
     * halt only the xHC endpoint; in that case CLEAR_FEATURE would be wrong. */
    if (clear_device_halt)
    {
        if (
            !xhci_control_no_data(
                controller,
                runtime_index,
                0x02u, /* Standard, Endpoint, Host-to-device */
                USB_REQUEST_CLEAR_FEATURE,
                USB_FEATURE_ENDPOINT_HALT,
                endpoint_address
            )
        )
        {
            return false;
        }
    }

    XhciTrb completion;
    if (
        !xhci_command(
            controller,
            0u,
            0u,
            (XHCI_TRB_TYPE_RESET_ENDPOINT << XHCI_TRB_TYPE_SHIFT)
            | ((u32)dci << 16)
            | ((u32)runtime->slot_id << 24),
            &completion
        )
        || xhci_completion_code(&completion) != XHCI_COMPLETION_SUCCESS
    )
    {
        return false;
    }

    /* Throw away the old producer state and give xHC a clean dequeue point. */
    xhci_prepare_normal_transfer_ring(ring);
    *enqueue = 0u;
    *cycle = 1u;
    if (endpoint_address == runtime->bulk_in_endpoint)
    {
        runtime->bulk_in_async_pending = false;
        runtime->bulk_in_async_complete = false;
        runtime->bulk_in_async_buffer = 0u;
        runtime->bulk_in_async_length = 0u;
    }
    xhci_barrier();

    if (
        !xhci_command(
            controller,
            xhci_physical(ring) | 1ull,
            0u,
            (XHCI_TRB_TYPE_SET_TR_DEQUEUE << XHCI_TRB_TYPE_SHIFT)
            | ((u32)dci << 16)
            | ((u32)runtime->slot_id << 24),
            &completion
        )
        || xhci_completion_code(&completion) != XHCI_COMPLETION_SUCCESS
    )
    {
        return false;
    }

    controller->endpoint_recoveries++;
    return true;
}

bool xhci_bulk_transfer_ex(
    u32 usb_device_id,
    u8 endpoint_address,
    void* buffer,
    usize length,
    usize* actual_length
)
{
    if (actual_length != NULL)
    {
        *actual_length = 0u;
    }

    int runtime_index_signed = xhci_find_runtime_by_usb_id(usb_device_id);

    if (
        runtime_index_signed < 0
        || buffer == NULL
        || length == 0u
        || length > 0x1FFFFu
    )
    {
        return false;
    }

    usize runtime_index = (usize)runtime_index_signed;
    XhciDeviceRuntime* runtime = &runtime_devices[runtime_index];

    if (
        !runtime->bulk_configured
        || runtime->controller_index >= XHCI_MAX_CONTROLLERS
    )
    {
        return false;
    }

    XhciController* controller = &controllers[runtime->controller_index];

    XhciTrb* ring = NULL;
    u16* enqueue = NULL;
    u8* cycle = NULL;
    u8 dci = 0u;

    if (
        endpoint_address == runtime->bulk_in_endpoint
        && (runtime->bulk_in_async_pending || runtime->bulk_in_async_complete)
    )
    {
        return false;
    }

    if (endpoint_address == runtime->bulk_out_endpoint)
    {
        ring = &bulk_out_rings[runtime_index][0];
        enqueue = &runtime->bulk_out_enqueue;
        cycle = &runtime->bulk_out_cycle;
        dci = runtime->bulk_out_dci;
    }
    else if (endpoint_address == runtime->bulk_in_endpoint)
    {
        ring = &bulk_in_rings[runtime_index][0];
        enqueue = &runtime->bulk_in_enqueue;
        cycle = &runtime->bulk_in_cycle;
        dci = runtime->bulk_in_dci;
    }
    else
    {
        return false;
    }


    for (u32 attempt = 0u; attempt < 2u; attempt++)
    {
        XhciTrb* trb = &ring[*enqueue];
        trb->parameter = xhci_physical(buffer);
        trb->status = (u32)length;
        trb->control =
            (XHCI_TRB_TYPE_NORMAL << XHCI_TRB_TYPE_SHIFT)
            | XHCI_TRB_IOC
            | XHCI_TRB_ISP
            | (u32)(*cycle);

        xhci_advance_normal_transfer_ring(ring, enqueue, cycle);
        xhci_barrier();

        xhci_write32(
            controller,
            controller->doorbell_offset + ((u32)runtime->slot_id * 4u),
            dci
        );

        XhciTrb completion;
        if (
            !xhci_wait_transfer_completion(
                controller,
                runtime->slot_id,
                dci,
                &completion
            )
        )
        {
            controller->transfer_errors++;
            return false;
        }

        u8 code = xhci_completion_code(&completion);
        if (
            code == XHCI_COMPLETION_SUCCESS
            || code == XHCI_COMPLETION_SHORT_PACKET
        )
        {
            u32 residual = completion.status & 0x00FFFFFFu;
            usize completed = length;
            if (residual <= length)
                completed = length - residual;

            if (actual_length != NULL)
                *actual_length = completed;

            return true;
        }

        controller->transfer_errors++;

        /* A STALL is recoverable without resetting the whole USB controller.
         * Rebuild only the affected endpoint and retry once. */
        if (
            attempt == 0u
            && code == XHCI_COMPLETION_STALL_ERROR
            && xhci_recover_bulk_endpoint(runtime_index, endpoint_address, true)
        )
        {
            /* Recovery reinitialized producer pointers; refresh aliases. */
            if (endpoint_address == runtime->bulk_out_endpoint)
            {
                ring = &bulk_out_rings[runtime_index][0];
                enqueue = &runtime->bulk_out_enqueue;
                cycle = &runtime->bulk_out_cycle;
            }
            else
            {
                ring = &bulk_in_rings[runtime_index][0];
                enqueue = &runtime->bulk_in_enqueue;
                cycle = &runtime->bulk_in_cycle;
            }
            continue;
        }

        /* USB transaction errors can leave the internal xHC endpoint halted
         * even when the device itself is not stalled. Linux xHCI checks the
         * endpoint context and performs host-side halt cleanup in this case.
         * Recover the ring/dequeue state. Replaying IN is safe; do not blindly
         * replay OUT because the device may already have observed the payload. */
        if (
            code == XHCI_COMPLETION_USB_TRANSACTION_ERROR
            && xhci_bulk_endpoint_state(runtime_index, dci) == XHCI_ENDPOINT_STATE_HALTED
        )
        {
            bool recovered = xhci_recover_bulk_endpoint(
                runtime_index,
                endpoint_address,
                false
            );

            if (recovered && (endpoint_address & 0x80u) != 0u && attempt == 0u)
            {
                if (endpoint_address == runtime->bulk_in_endpoint)
                {
                    ring = &bulk_in_rings[runtime_index][0];
                    enqueue = &runtime->bulk_in_enqueue;
                    cycle = &runtime->bulk_in_cycle;
                }
                continue;
            }
        }

        /* Babble/context/other errors are returned to the class driver. */
        return false;
    }

    return false;
}

bool xhci_bulk_receive_poll(
    u32 usb_device_id,
    u8 endpoint_address,
    void* buffer,
    usize length,
    usize* actual_length,
    bool* completed
)
{
    if (actual_length != NULL)
        *actual_length = 0u;
    if (completed != NULL)
        *completed = false;

    if (
        buffer == NULL
        || length == 0u
        || length > 0x1FFFFu
        || completed == NULL
        || (endpoint_address & 0x80u) == 0u
    )
    {
        return false;
    }

    /* Network commands may spend significant time outside shell_run(). Keep
     * root-port disconnect/reconnect handling alive while polling RX. */
    xhci_poll();

    int runtime_index_signed = xhci_find_runtime_by_usb_id(usb_device_id);
    if (runtime_index_signed < 0)
        return false;

    usize runtime_index = (usize)runtime_index_signed;
    XhciDeviceRuntime* runtime = &runtime_devices[runtime_index];
    if (
        !runtime->bulk_configured
        || endpoint_address != runtime->bulk_in_endpoint
        || runtime->controller_index >= XHCI_MAX_CONTROLLERS
    )
    {
        return false;
    }

    XhciController* controller = &controllers[runtime->controller_index];
    if (!controller->used || !controller->running)
        return false;

    /* Pull any already-generated completion without waiting. */
    xhci_collect_idle_events(controller, 16u);

    if (runtime->bulk_in_async_complete)
    {
        XhciTrb completion = runtime->bulk_in_async_event;
        u8 code = xhci_completion_code(&completion);
        usize requested = runtime->bulk_in_async_length;

        runtime->bulk_in_async_pending = false;
        runtime->bulk_in_async_complete = false;
        runtime->bulk_in_async_buffer = 0u;
        runtime->bulk_in_async_length = 0u;

        if (
            code == XHCI_COMPLETION_SUCCESS
            || code == XHCI_COMPLETION_SHORT_PACKET
        )
        {
            u32 residual = completion.status & 0x00FFFFFFu;
            usize done = requested;
            if (residual <= requested)
                done = requested - residual;

            if (actual_length != NULL)
                *actual_length = done;
            *completed = true;
            return true;
        }

        controller->transfer_errors++;

        if (code == XHCI_COMPLETION_STALL_ERROR)
        {
            (void)xhci_recover_bulk_endpoint(runtime_index, endpoint_address, true);
        }
        else if (
            code == XHCI_COMPLETION_USB_TRANSACTION_ERROR
            && xhci_bulk_endpoint_state(runtime_index, runtime->bulk_in_dci)
                == XHCI_ENDPOINT_STATE_HALTED
        )
        {
            (void)xhci_recover_bulk_endpoint(runtime_index, endpoint_address, false);
        }

        return false;
    }

    if (runtime->bulk_in_async_pending)
    {
        if (
            runtime->bulk_in_async_buffer != xhci_physical(buffer)
            || runtime->bulk_in_async_length != length
        )
        {
            return false;
        }

        /* No completion yet means normal USB NAK/backpressure, not an error. */
        return true;
    }

    XhciTrb* ring = &bulk_in_rings[runtime_index][0];
    XhciTrb* trb = &ring[runtime->bulk_in_enqueue];
    trb->parameter = xhci_physical(buffer);
    trb->status = (u32)length;
    trb->control =
        (XHCI_TRB_TYPE_NORMAL << XHCI_TRB_TYPE_SHIFT)
        | XHCI_TRB_IOC
        | XHCI_TRB_ISP
        | (u32)runtime->bulk_in_cycle;

    xhci_advance_normal_transfer_ring(
        ring,
        &runtime->bulk_in_enqueue,
        &runtime->bulk_in_cycle
    );

    runtime->bulk_in_async_pending = true;
    runtime->bulk_in_async_complete = false;
    runtime->bulk_in_async_buffer = xhci_physical(buffer);
    runtime->bulk_in_async_length = length;

    xhci_barrier();
    xhci_write32(
        controller,
        controller->doorbell_offset + ((u32)runtime->slot_id * 4u),
        runtime->bulk_in_dci
    );

    return true;
}


bool xhci_bulk_transfer(
    u32 usb_device_id,
    u8 endpoint_address,
    void* buffer,
    usize length
)
{
    return xhci_bulk_transfer_ex(
        usb_device_id,
        endpoint_address,
        buffer,
        length,
        NULL
    );
}

const char* xhci_link_state_name(u8 state)
{
    switch (state)
    {
        case 0u: return "U0";
        case 1u: return "U1";
        case 2u: return "U2";
        case 3u: return "U3";
        case 4u: return "Disabled";
        case 5u: return "RxDetect";
        case 6u: return "Inactive";
        case 7u: return "Polling";
        case 8u: return "Recovery";
        case 9u: return "HotReset";
        case 10u: return "Compliance";
        case 11u: return "Test";
        case 15u: return "Resume";
        default: return "Reserved";
    }
}

const char* xhci_protocol_name(u8 major)
{
    if (major >= 3u)
    {
        return "USB3";
    }

    if (major == 2u)
    {
        return "USB2";
    }

    return "USB";
}


const char* xhci_error_name(u8 error)
{
    switch (error)
    {
        case XHCI_ERROR_NONE: return "none";
        case XHCI_ERROR_BAD_BAR: return "bad BAR";
        case XHCI_ERROR_PCI_ENABLE: return "PCI enable failed";
        case XHCI_ERROR_BAD_CAPS: return "invalid capabilities";
        case XHCI_ERROR_RESET_TIMEOUT: return "reset timeout";
        case XHCI_ERROR_PAGE_SIZE: return "4 KiB pages unsupported";
        case XHCI_ERROR_START_TIMEOUT: return "start timeout";
        case XHCI_ERROR_NOT_READY: return "controller not ready";
        default: return "unknown";
    }
}
