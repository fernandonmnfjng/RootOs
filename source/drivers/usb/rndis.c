#include "rndis.h"

#include "usb.h"
#include "xhci.h"
#include "net_device.h"
#include "memory.h"
#include "time.h"

#define RNDIS_CONTROL_BUFFER_SIZE 1024u
#define RNDIS_RX_BUFFER_SIZE 4096u
#define RNDIS_TX_BUFFER_SIZE 2048u
#define RNDIS_ETH_FRAME_MAX 1600u

#define USB_REQTYPE_CLASS_INTERFACE_OUT 0x21u
#define USB_REQTYPE_CLASS_INTERFACE_IN  0xA1u
#define USB_CDC_SEND_ENCAPSULATED_COMMAND 0x00u
#define USB_CDC_GET_ENCAPSULATED_RESPONSE 0x01u

#define RNDIS_MSG_PACKET       0x00000001u
#define RNDIS_MSG_INIT         0x00000002u
#define RNDIS_MSG_HALT         0x00000003u
#define RNDIS_MSG_QUERY        0x00000004u
#define RNDIS_MSG_SET          0x00000005u
#define RNDIS_MSG_INDICATE     0x00000007u
#define RNDIS_MSG_KEEPALIVE    0x00000008u

#define RNDIS_MSG_INIT_C       0x80000002u
#define RNDIS_MSG_QUERY_C      0x80000004u
#define RNDIS_MSG_SET_C        0x80000005u
#define RNDIS_MSG_KEEPALIVE_C  0x80000008u

#define RNDIS_STATUS_SUCCESS 0x00000000u

#define RNDIS_OID_GEN_CURRENT_PACKET_FILTER 0x0001010Eu
#define RNDIS_OID_GEN_MEDIA_CONNECT_STATUS  0x00010114u
#define RNDIS_OID_802_3_PERMANENT_ADDRESS   0x01010101u

#define RNDIS_PACKET_TYPE_DIRECTED      0x00000001u
#define RNDIS_PACKET_TYPE_ALL_MULTICAST 0x00000004u
#define RNDIS_PACKET_TYPE_BROADCAST     0x00000008u
#define RNDIS_DEFAULT_FILTER \
    (RNDIS_PACKET_TYPE_DIRECTED | RNDIS_PACKET_TYPE_ALL_MULTICAST | RNDIS_PACKET_TYPE_BROADCAST)

#define RNDIS_MEDIA_STATE_CONNECTED    0x00000000u
#define RNDIS_MEDIA_STATE_DISCONNECTED 0x00000001u

#define USB_CLASS_CDC_DATA 0x0Au
#define USB_CLASS_WIRELESS 0xE0u
#define USB_RNDIS_SUBCLASS 0x01u
#define USB_RNDIS_PROTOCOL 0x03u

#define USB_ENDPOINT_BULK 0x02u

/* Xiaomi's Android RNDIS identity observed on the target machine.  The class
 * match below is generic; this VID:PID is only used as an additional hint. */
#define XIAOMI_VENDOR_ID 0x2717u
#define XIAOMI_RNDIS_PRODUCT_ID 0xFF80u

typedef struct
{
    bool used;
    bool registered;
    bool usb_configured;
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

    u32 request_id;
    u8 mac[6];

    u8 control_buffer[RNDIS_CONTROL_BUFFER_SIZE] __attribute__((aligned(16)));
    u8 rx_buffer[RNDIS_RX_BUFFER_SIZE] __attribute__((aligned(16)));
    usize rx_length;
    usize rx_offset;
    u8 tx_buffer[RNDIS_TX_BUFFER_SIZE] __attribute__((aligned(16)));

    const char* error;

    u32 io_failures;
    u64 retry_at_ms;
} RndisRuntime;

static RndisRuntime devices[RNDIS_MAX_DEVICES];
static usize device_count = 0u;
static const char* global_error = "none";

#define RNDIS_IO_FAILURE_LIMIT 3u
#define RNDIS_RETRY_DELAY_MS 250u

static void mark_transport_failure(RndisRuntime* runtime, const char* error)
{
    if (runtime == NULL)
        return;

    if (runtime->io_failures < 0xFFFFFFFFu)
        runtime->io_failures++;

    runtime->error = error;
    global_error = error;

    if (runtime->io_failures >= RNDIS_IO_FAILURE_LIMIT)
    {
        runtime->ready = false;
        runtime->link_up = false;
        runtime->rx_length = 0u;
        runtime->rx_offset = 0u;
        runtime->retry_at_ms = root_time_millis() + RNDIS_RETRY_DELAY_MS;
    }
}

static void mark_transport_success(RndisRuntime* runtime)
{
    if (runtime == NULL)
        return;
    runtime->io_failures = 0u;
}

static u32 read_le32(const u8* data)
{
    return
        (u32)data[0]
        | ((u32)data[1] << 8)
        | ((u32)data[2] << 16)
        | ((u32)data[3] << 24);
}

static void write_le32(u8* data, u32 value)
{
    data[0] = (u8)(value & 0xFFu);
    data[1] = (u8)((value >> 8) & 0xFFu);
    data[2] = (u8)((value >> 16) & 0xFFu);
    data[3] = (u8)((value >> 24) & 0xFFu);
}

static bool mac_valid(const u8 mac[6])
{
    bool any = false;
    bool all_ff = true;

    for (u32 i = 0u; i < 6u; i++)
    {
        if (mac[i] != 0u)
            any = true;
        if (mac[i] != 0xFFu)
            all_ff = false;
    }

    return any && !all_ff && (mac[0] & 0x01u) == 0u;
}

static void wait_millis(u32 milliseconds)
{
    u64 deadline = root_time_millis() + milliseconds;
    while (root_time_millis() < deadline)
        __asm__ volatile("pause");
}

static u32 next_request_id(RndisRuntime* runtime)
{
    runtime->request_id++;
    if (runtime->request_id == 0u)
        runtime->request_id = 1u;
    return runtime->request_id;
}

static bool send_control(
    RndisRuntime* runtime,
    const void* message,
    u16 message_length
)
{
    usize actual = 0u;
    return xhci_control_transfer(
        runtime->usb_device_id,
        USB_REQTYPE_CLASS_INTERFACE_OUT,
        USB_CDC_SEND_ENCAPSULATED_COMMAND,
        0u,
        runtime->control_interface,
        (void*)message,
        message_length,
        &actual
    );
}

static bool receive_control_response(
    RndisRuntime* runtime,
    u32 expected_type,
    u32 expected_request_id,
    u8** response,
    usize* response_length
)
{
    if (response == NULL || response_length == NULL)
        return false;

    for (u32 attempt = 0u; attempt < 20u; attempt++)
    {
        root_memzero(runtime->control_buffer, sizeof(runtime->control_buffer));

        usize actual = 0u;
        bool ok = xhci_control_transfer(
            runtime->usb_device_id,
            USB_REQTYPE_CLASS_INTERFACE_IN,
            USB_CDC_GET_ENCAPSULATED_RESPONSE,
            0u,
            runtime->control_interface,
            runtime->control_buffer,
            (u16)sizeof(runtime->control_buffer),
            &actual
        );

        if (ok)
        {
            u32 type = read_le32(runtime->control_buffer + 0u);
            u32 length = read_le32(runtime->control_buffer + 4u);

            if (length >= 8u && length <= sizeof(runtime->control_buffer))
            {
                if (type == RNDIS_MSG_INDICATE)
                {
                    /* Link/media indications are optional for this first
                     * polling driver.  Ignore them and wait for our reply. */
                }
                else if (type == RNDIS_MSG_KEEPALIVE)
                {
                    /* Devices rarely send this on the host response path.
                     * A full asynchronous control channel will answer it in
                     * a later networking revision. */
                }
                else if (type == expected_type)
                {
                    if (
                        expected_request_id == 0u
                        || (length >= 12u && read_le32(runtime->control_buffer + 8u) == expected_request_id)
                    )
                    {
                        *response = runtime->control_buffer;
                        *response_length = length;
                        return true;
                    }
                }
            }
        }

        wait_millis(10u);
    }

    return false;
}

static bool rndis_initialize_protocol(RndisRuntime* runtime)
{
    u8 request[24];
    root_memzero(request, sizeof(request));

    u32 request_id = next_request_id(runtime);
    write_le32(request + 0u, RNDIS_MSG_INIT);
    write_le32(request + 4u, sizeof(request));
    write_le32(request + 8u, request_id);
    write_le32(request + 12u, 1u);
    write_le32(request + 16u, 0u);
    write_le32(request + 20u, RNDIS_RX_BUFFER_SIZE);

    if (!send_control(runtime, request, sizeof(request)))
    {
        runtime->error = "RNDIS INIT command failed";
        return false;
    }

    u8* response = NULL;
    usize response_length = 0u;
    if (
        !receive_control_response(
            runtime,
            RNDIS_MSG_INIT_C,
            request_id,
            &response,
            &response_length
        )
        || response_length < 16u
    )
    {
        runtime->error = "RNDIS INIT response timeout";
        return false;
    }

    if (read_le32(response + 12u) != RNDIS_STATUS_SUCCESS)
    {
        runtime->error = "RNDIS INIT rejected";
        return false;
    }

    return true;
}

static bool rndis_query(
    RndisRuntime* runtime,
    u32 oid,
    u32 input_length,
    const u8** output,
    usize* output_length
)
{
    if (
        output == NULL
        || output_length == NULL
        || 28u + input_length > sizeof(runtime->control_buffer)
    )
    {
        return false;
    }

    u8 request[RNDIS_CONTROL_BUFFER_SIZE];
    root_memzero(request, sizeof(request));

    u32 request_id = next_request_id(runtime);
    u32 message_length = 28u + input_length;

    write_le32(request + 0u, RNDIS_MSG_QUERY);
    write_le32(request + 4u, message_length);
    write_le32(request + 8u, request_id);
    write_le32(request + 12u, oid);
    write_le32(request + 16u, input_length);
    write_le32(request + 20u, 20u); /* from RequestId field, per RNDIS */
    write_le32(request + 24u, 0u);

    if (!send_control(runtime, request, (u16)message_length))
        return false;

    u8* response = NULL;
    usize response_length = 0u;
    if (
        !receive_control_response(
            runtime,
            RNDIS_MSG_QUERY_C,
            request_id,
            &response,
            &response_length
        )
        || response_length < 24u
        || read_le32(response + 12u) != RNDIS_STATUS_SUCCESS
    )
    {
        return false;
    }

    u32 information_length = read_le32(response + 16u);
    u32 information_offset = read_le32(response + 20u);

    /* InformationBufferOffset is measured from the RequestId field, which is
     * eight bytes into the RNDIS message. */
    u32 absolute_offset = 8u + information_offset;

    if (
        absolute_offset > response_length
        || information_length > response_length - absolute_offset
    )
    {
        return false;
    }

    *output = response + absolute_offset;
    *output_length = information_length;
    return true;
}

static bool rndis_set_u32(RndisRuntime* runtime, u32 oid, u32 value)
{
    u8 request[32];
    root_memzero(request, sizeof(request));

    u32 request_id = next_request_id(runtime);
    write_le32(request + 0u, RNDIS_MSG_SET);
    write_le32(request + 4u, sizeof(request));
    write_le32(request + 8u, request_id);
    write_le32(request + 12u, oid);
    write_le32(request + 16u, 4u);
    write_le32(request + 20u, 20u);
    write_le32(request + 24u, 0u);
    write_le32(request + 28u, value);

    if (!send_control(runtime, request, sizeof(request)))
        return false;

    u8* response = NULL;
    usize response_length = 0u;
    return
        receive_control_response(
            runtime,
            RNDIS_MSG_SET_C,
            request_id,
            &response,
            &response_length
        )
        && response_length >= 16u
        && read_le32(response + 12u) == RNDIS_STATUS_SUCCESS;
}

static bool find_rndis_interfaces(
    const UsbDeviceInfo* usb,
    u8* control_interface,
    u8* data_interface,
    u8* data_alternate_setting,
    u8* bulk_out,
    u16* bulk_out_packet,
    u8* bulk_in,
    u16* bulk_in_packet
)
{
    bool have_control = false;
    bool have_data = false;

    for (u8 i = 0u; i < usb->interface_count; i++)
    {
        const UsbInterfaceInfo* interface = &usb->interfaces[i];

        if (
            !have_control
            && interface->class_code == USB_CLASS_WIRELESS
            && interface->subclass == USB_RNDIS_SUBCLASS
            && interface->protocol == USB_RNDIS_PROTOCOL
        )
        {
            *control_interface = interface->number;
            have_control = true;
        }
    }

    /* Some Android implementations are known by VID:PID but may expose
     * slightly unusual control class descriptors.  Permit that identity only
     * as a narrow fallback, never as a generic vendor-class match. */
    if (
        !have_control
        && usb->vendor_id == XIAOMI_VENDOR_ID
        && usb->product_id == XIAOMI_RNDIS_PRODUCT_ID
    )
    {
        for (u8 i = 0u; i < usb->interface_count; i++)
        {
            const UsbInterfaceInfo* interface = &usb->interfaces[i];
            if (interface->class_code != USB_CLASS_CDC_DATA)
            {
                *control_interface = interface->number;
                have_control = true;
                break;
            }
        }
    }

    if (!have_control)
        return false;

    for (u8 i = 0u; i < usb->interface_count; i++)
    {
        const UsbInterfaceInfo* interface = &usb->interfaces[i];
        if (interface->class_code != USB_CLASS_CDC_DATA)
            continue;

        u8 candidate_out = 0u;
        u8 candidate_in = 0u;
        u16 candidate_out_packet = 0u;
        u16 candidate_in_packet = 0u;

        for (u8 e = 0u; e < interface->endpoint_count; e++)
        {
            const UsbEndpointInfo* endpoint = &interface->endpoints[e];
            if ((endpoint->attributes & 0x03u) != USB_ENDPOINT_BULK)
                continue;

            if ((endpoint->address & 0x80u) != 0u)
            {
                candidate_in = endpoint->address;
                candidate_in_packet = endpoint->max_packet_size;
            }
            else
            {
                candidate_out = endpoint->address;
                candidate_out_packet = endpoint->max_packet_size;
            }
        }

        if (
            candidate_out != 0u
            && candidate_in != 0u
            && candidate_out_packet != 0u
            && candidate_in_packet != 0u
        )
        {
            *data_interface = interface->number;
            *data_alternate_setting = interface->alternate_setting;
            *bulk_out = candidate_out;
            *bulk_out_packet = candidate_out_packet;
            *bulk_in = candidate_in;
            *bulk_in_packet = candidate_in_packet;
            have_data = true;
            break;
        }
    }

    return have_data;
}

static bool rndis_net_ready(void* context)
{
    RndisRuntime* runtime = (RndisRuntime*)context;
    return runtime != NULL && runtime->ready;
}

static bool rndis_net_link(void* context)
{
    RndisRuntime* runtime = (RndisRuntime*)context;
    return runtime != NULL && runtime->ready && runtime->link_up;
}

static bool rndis_net_send(void* context, const void* data, usize size)
{
    RndisRuntime* runtime = (RndisRuntime*)context;

    if (
        runtime == NULL
        || !runtime->ready
        || data == NULL
        || size == 0u
        || size > RNDIS_ETH_FRAME_MAX
        || 44u + size > sizeof(runtime->tx_buffer)
    )
    {
        return false;
    }

    root_memzero(runtime->tx_buffer, 44u);
    write_le32(runtime->tx_buffer + 0u, RNDIS_MSG_PACKET);
    write_le32(runtime->tx_buffer + 4u, (u32)(44u + size));
    write_le32(runtime->tx_buffer + 8u, 36u); /* sizeof(header) - 8 */
    write_le32(runtime->tx_buffer + 12u, (u32)size);
    root_memcpy(runtime->tx_buffer + 44u, data, size);

    bool ok = xhci_bulk_transfer(
        runtime->usb_device_id,
        runtime->bulk_out_endpoint,
        runtime->tx_buffer,
        44u + size
    );

    if (!ok)
    {
        mark_transport_failure(runtime, "RNDIS USB TX failed");
        return false;
    }

    mark_transport_success(runtime);
    return true;
}

static bool extract_rndis_packet(
    RndisRuntime* runtime,
    void* output,
    usize capacity,
    usize* result_size
)
{
    while (runtime->rx_offset + 8u <= runtime->rx_length)
    {
        u8* message = runtime->rx_buffer + runtime->rx_offset;
        usize remaining = runtime->rx_length - runtime->rx_offset;
        u32 type = read_le32(message + 0u);
        u32 message_length = read_le32(message + 4u);

        if (message_length < 8u || message_length > remaining)
        {
            runtime->rx_length = 0u;
            runtime->rx_offset = 0u;
            return false;
        }

        runtime->rx_offset += message_length;

        if (type != RNDIS_MSG_PACKET || message_length < 44u)
            continue;

        u32 data_offset = read_le32(message + 8u);
        u32 data_length = read_le32(message + 12u);
        u32 absolute_offset = 8u + data_offset;

        if (
            absolute_offset > message_length
            || data_length > message_length - absolute_offset
            || data_length > capacity
        )
        {
            continue;
        }

        root_memcpy(output, message + absolute_offset, data_length);
        if (result_size != NULL)
            *result_size = data_length;

        if (runtime->rx_offset >= runtime->rx_length)
        {
            runtime->rx_length = 0u;
            runtime->rx_offset = 0u;
        }

        return true;
    }

    runtime->rx_length = 0u;
    runtime->rx_offset = 0u;
    return false;
}

static bool rndis_net_receive(
    void* context,
    void* output,
    usize capacity,
    usize* result_size
)
{
    RndisRuntime* runtime = (RndisRuntime*)context;

    if (result_size != NULL)
        *result_size = 0u;

    if (
        runtime == NULL
        || !runtime->ready
        || output == NULL
        || capacity == 0u
    )
    {
        return false;
    }

    if (runtime->rx_length > runtime->rx_offset)
    {
        if (extract_rndis_packet(runtime, output, capacity, result_size))
            return true;
    }

    usize actual = 0u;
    bool completed = false;

    bool transfer_ok = xhci_bulk_receive_poll(
        runtime->usb_device_id,
        runtime->bulk_in_endpoint,
        runtime->rx_buffer,
        sizeof(runtime->rx_buffer),
        &actual,
        &completed
    );

    if (!transfer_ok)
    {
        mark_transport_failure(runtime, "RNDIS USB RX failed");
        return false;
    }

    /* No completion yet is the normal idle state: the device is NAKing until
     * an Ethernet frame arrives. Never count that as a USB failure. */
    if (!completed)
        return false;

    mark_transport_success(runtime);
    if (actual < 8u)
        return false;

    runtime->rx_length = actual;
    runtime->rx_offset = 0u;
    return extract_rndis_packet(runtime, output, capacity, result_size);
}

static const RootNetDeviceOps net_ops =
{
    .ready = rndis_net_ready,
    .link_up = rndis_net_link,
    .send_frame = rndis_net_send,
    .receive_frame = rndis_net_receive
};

static RndisRuntime* find_runtime(u32 usb_device_id)
{
    for (usize i = 0u; i < RNDIS_MAX_DEVICES; i++)
    {
        if (devices[i].used && devices[i].usb_device_id == usb_device_id)
            return &devices[i];
    }
    return NULL;
}

static RndisRuntime* allocate_runtime(u32 usb_device_id)
{
    RndisRuntime* existing = find_runtime(usb_device_id);
    if (existing != NULL)
        return existing;

    for (usize i = 0u; i < RNDIS_MAX_DEVICES; i++)
    {
        if (!devices[i].used)
        {
            root_memzero(&devices[i], sizeof(devices[i]));
            devices[i].used = true;
            devices[i].usb_device_id = usb_device_id;
            devices[i].error = "not initialized";
            device_count++;
            return &devices[i];
        }
    }

    return NULL;
}

static bool initialize_device(const UsbDeviceInfo* usb)
{
    u8 control_interface = 0u;
    u8 data_interface = 0u;
    u8 data_alternate_setting = 0u;
    u8 bulk_out = 0u;
    u16 bulk_out_packet = 0u;
    u8 bulk_in = 0u;
    u16 bulk_in_packet = 0u;

    if (
        !find_rndis_interfaces(
            usb,
            &control_interface,
            &data_interface,
            &data_alternate_setting,
            &bulk_out,
            &bulk_out_packet,
            &bulk_in,
            &bulk_in_packet
        )
    )
    {
        return false;
    }

    RndisRuntime* runtime = allocate_runtime(usb->id);
    if (runtime == NULL)
    {
        global_error = "RNDIS device table full";
        return false;
    }

    if (runtime->ready)
        return true;

    runtime->vendor_id = usb->vendor_id;
    runtime->product_id = usb->product_id;
    runtime->control_interface = control_interface;
    runtime->data_interface = data_interface;
    runtime->data_alternate_setting = data_alternate_setting;
    runtime->bulk_out_endpoint = bulk_out;
    runtime->bulk_in_endpoint = bulk_in;
    runtime->bulk_out_packet_size = bulk_out_packet;
    runtime->bulk_in_packet_size = bulk_in_packet;
    runtime->request_id = 0u;
    runtime->rx_length = 0u;
    runtime->rx_offset = 0u;
    runtime->link_up = false;
    runtime->io_failures = 0u;
    runtime->retry_at_ms = 0u;
    runtime->error = "configuring USB bulk endpoints";

    if (!runtime->usb_configured)
    {
        if (
            !xhci_configure_bulk_interface(
                usb->id,
                usb->configuration_value != 0u ? usb->configuration_value : 1u,
                data_interface,
                data_alternate_setting,
                bulk_out,
                bulk_out_packet,
                bulk_in,
                bulk_in_packet
            )
        )
        {
            runtime->error = "xHCI bulk endpoint configuration failed";
            global_error = runtime->error;
            return false;
        }

        runtime->usb_configured = true;
    }

    runtime->error = "initializing RNDIS protocol";
    if (!rndis_initialize_protocol(runtime))
    {
        global_error = runtime->error;
        return false;
    }

    /* Linux intentionally supplies a 48-byte zero query payload for this OID
     * because some RNDIS implementations require room matching the expected
     * response.  It is harmless for normal Android RNDIS devices. */
    const u8* query_data = NULL;
    usize query_length = 0u;

    runtime->error = "querying RNDIS MAC address";
    if (
        !rndis_query(
            runtime,
            RNDIS_OID_802_3_PERMANENT_ADDRESS,
            48u,
            &query_data,
            &query_length
        )
        || query_length != 6u
    )
    {
        global_error = runtime->error;
        return false;
    }

    root_memcpy(runtime->mac, query_data, 6u);
    if (!mac_valid(runtime->mac))
    {
        runtime->error = "RNDIS returned invalid MAC address";
        global_error = runtime->error;
        return false;
    }

    runtime->error = "enabling RNDIS packet filter";
    if (
        !rndis_set_u32(
            runtime,
            RNDIS_OID_GEN_CURRENT_PACKET_FILTER,
            RNDIS_DEFAULT_FILTER
        )
    )
    {
        global_error = runtime->error;
        return false;
    }

    /* Optional media-status query.  Failure is not fatal because Android
     * tethering devices frequently do not expose every optional OID. */
    query_data = NULL;
    query_length = 0u;
    if (
        rndis_query(
            runtime,
            RNDIS_OID_GEN_MEDIA_CONNECT_STATUS,
            4u,
            &query_data,
            &query_length
        )
        && query_length >= 4u
    )
    {
        u32 media = read_le32(query_data);
        runtime->link_up = media != RNDIS_MEDIA_STATE_DISCONNECTED;
    }
    else
    {
        runtime->link_up = true;
    }

    runtime->ready = true;
    runtime->error = "none";
    global_error = "none";

    if (!runtime->registered)
    {
        if (!net_device_register("rndis", runtime, &net_ops, runtime->mac))
        {
            runtime->ready = false;
            runtime->error = "could not register network adapter";
            global_error = runtime->error;
            return false;
        }
        runtime->registered = true;
    }

    return true;
}

static void rndis_usb_added(const UsbDeviceInfo* usb)
{
    if (usb == NULL)
        return;

    (void)initialize_device(usb);
}

static void rndis_usb_removed(u32 usb_device_id)
{
    RndisRuntime* runtime = find_runtime(usb_device_id);
    if (runtime == NULL)
        return;

    runtime->usb_configured = false;
    runtime->ready = false;
    runtime->link_up = false;
    runtime->rx_length = 0u;
    runtime->rx_offset = 0u;
    runtime->io_failures = 0u;
    runtime->retry_at_ms = root_time_millis() + RNDIS_RETRY_DELAY_MS;
    runtime->error = "USB device disconnected; waiting for reconnect";
    global_error = runtime->error;
}

void rndis_init(void)
{
    root_memzero(devices, sizeof(devices));
    device_count = 0u;
    global_error = "none";

    if (!usb_register_listener(rndis_usb_added, rndis_usb_removed))
        global_error = "could not register RNDIS USB lifecycle listener";
}

void rndis_service(void)
{
    u64 now = root_time_millis();

    for (usize i = 0u; i < RNDIS_MAX_DEVICES; i++)
    {
        RndisRuntime* runtime = &devices[i];
        if (!runtime->used)
            continue;

        if (!usb_device_present(runtime->usb_device_id))
        {
            if (runtime->ready || runtime->link_up)
                rndis_usb_removed(runtime->usb_device_id);
            continue;
        }

        if (runtime->ready || now < runtime->retry_at_ms)
            continue;

        UsbDeviceInfo usb;
        if (!usb_get_device_by_id(runtime->usb_device_id, &usb))
            continue;

        /* If the device never disconnected, endpoint contexts remain valid.
         * A failed control/RNDIS session can therefore be renegotiated without
         * destructively reconfiguring the xHCI endpoints. On a real reconnect,
         * rndis_usb_removed() cleared usb_configured first. */
        if (!initialize_device(&usb))
            runtime->retry_at_ms = now + RNDIS_RETRY_DELAY_MS;
    }
}

void rndis_invalidate_all(void)
{
    for (usize i = 0u; i < RNDIS_MAX_DEVICES; i++)
    {
        if (!devices[i].used)
            continue;

        devices[i].usb_configured = false;
        devices[i].ready = false;
        devices[i].link_up = false;
        devices[i].rx_length = 0u;
        devices[i].rx_offset = 0u;
        devices[i].io_failures = 0u;
        devices[i].retry_at_ms = root_time_millis() + RNDIS_RETRY_DELAY_MS;
        devices[i].error = "USB controller was rescanned";
    }
}

bool rndis_probe_all(void)
{
    bool found_candidate = false;
    bool initialized = false;

    for (usize i = 0u; i < usb_device_count(); i++)
    {
        UsbDeviceInfo usb;
        if (!usb_get_device(i, &usb))
            continue;

        u8 control_interface = 0u;
        u8 data_interface = 0u;
        u8 alternate = 0u;
        u8 bulk_out = 0u;
        u16 bulk_out_packet = 0u;
        u8 bulk_in = 0u;
        u16 bulk_in_packet = 0u;

        if (
            !find_rndis_interfaces(
                &usb,
                &control_interface,
                &data_interface,
                &alternate,
                &bulk_out,
                &bulk_out_packet,
                &bulk_in,
                &bulk_in_packet
            )
        )
        {
            continue;
        }

        found_candidate = true;
        if (initialize_device(&usb))
            initialized = true;
    }

    if (!found_candidate)
        global_error = "no RNDIS USB interface found";

    return initialized;
}

usize rndis_device_count(void)
{
    return device_count;
}

bool rndis_get_device(usize index, RndisDeviceInfo* output)
{
    if (output == NULL)
        return false;

    usize visible = 0u;
    for (usize i = 0u; i < RNDIS_MAX_DEVICES; i++)
    {
        RndisRuntime* runtime = &devices[i];
        if (!runtime->used)
            continue;

        if (visible++ != index)
            continue;

        root_memzero(output, sizeof(*output));
        output->used = true;
        output->ready = runtime->ready;
        output->link_up = runtime->link_up;
        output->usb_device_id = runtime->usb_device_id;
        output->vendor_id = runtime->vendor_id;
        output->product_id = runtime->product_id;
        output->control_interface = runtime->control_interface;
        output->data_interface = runtime->data_interface;
        output->data_alternate_setting = runtime->data_alternate_setting;
        output->bulk_in_endpoint = runtime->bulk_in_endpoint;
        output->bulk_out_endpoint = runtime->bulk_out_endpoint;
        output->bulk_in_packet_size = runtime->bulk_in_packet_size;
        output->bulk_out_packet_size = runtime->bulk_out_packet_size;
        root_memcpy(output->mac, runtime->mac, 6u);
        output->last_error = runtime->error;
        return true;
    }

    return false;
}

const char* rndis_last_error(void)
{
    return global_error;
}
