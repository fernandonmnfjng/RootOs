#include "usb_mass_storage.h"

#include "xhci.h"
#include "usb.h"
#include "block_device.h"
#include "memory.h"
#include "string.h"

#define USB_CLASS_MASS_STORAGE 0x08u
#define USB_MASS_PROTOCOL_BULK_ONLY 0x50u

#define USB_ENDPOINT_TRANSFER_MASK 0x03u
#define USB_ENDPOINT_TRANSFER_BULK 0x02u
#define USB_ENDPOINT_DIRECTION_IN 0x80u

#define USB_MASS_CBW_SIGNATURE 0x43425355u
#define USB_MASS_CSW_SIGNATURE 0x53425355u

#define SCSI_TEST_UNIT_READY 0x00u
#define SCSI_INQUIRY 0x12u
#define SCSI_READ_CAPACITY_10 0x25u
#define SCSI_READ_10 0x28u

#define USB_MASS_MAX_TRANSFER_BLOCKS 16u

#pragma pack(push, 1)
typedef struct
{
    u32 signature;
    u32 tag;
    u32 transfer_length;
    u8 flags;
    u8 lun;
    u8 command_length;
    u8 command[16];
} UsbMassCbw;

typedef struct
{
    u32 signature;
    u32 tag;
    u32 residue;
    u8 status;
} UsbMassCsw;
#pragma pack(pop)

_Static_assert(sizeof(UsbMassCbw) == 31u, "USB BOT CBW must be 31 bytes");
_Static_assert(sizeof(UsbMassCsw) == 13u, "USB BOT CSW must be 13 bytes");

typedef struct
{
    bool used;
    u32 usb_device_id;
    u32 block_device_id;

    u8 interface_number;
    u8 bulk_in_endpoint;
    u8 bulk_out_endpoint;

    u16 bulk_in_packet;
    u16 bulk_out_packet;

    u32 block_size;
    u64 block_count;

    u32 next_tag;

    char vendor[9];
    char product[17];
    char revision[5];
} UsbMassRuntime;

static UsbMassRuntime mass_devices[USB_MASS_MAX_DEVICES];

static u32 be32_read(const u8* data)
{
    return
        ((u32)data[0] << 24)
        |
        ((u32)data[1] << 16)
        |
        ((u32)data[2] << 8)
        |
        (u32)data[3];
}

static void be32_write(u8* data, u32 value)
{
    data[0] = (u8)(value >> 24);
    data[1] = (u8)(value >> 16);
    data[2] = (u8)(value >> 8);
    data[3] = (u8)value;
}

static void be16_write(u8* data, u16 value)
{
    data[0] = (u8)(value >> 8);
    data[1] = (u8)value;
}

static void copy_trim_ascii(
    char* output,
    usize output_size,
    const u8* input,
    usize input_size
)
{
    if (output == NULL || output_size == 0u)
    {
        return;
    }

    usize length = input_size;

    while (
        length > 0u
        && (
            input[length - 1u] == ' '
            || input[length - 1u] == '\0'
        )
    )
    {
        length--;
    }

    if (length >= output_size)
    {
        length = output_size - 1u;
    }

    if (length > 0u)
    {
        root_memcpy(output, input, length);
    }

    output[length] = '\0';
}

static bool mass_bot_command(
    UsbMassRuntime* device,
    const u8* command,
    u8 command_length,
    void* data,
    u32 data_length,
    bool data_in
)
{
    if (
        device == NULL
        || command == NULL
        || command_length == 0u
        || command_length > 16u
        || (data_length > 0u && data == NULL)
    )
    {
        return false;
    }

    UsbMassCbw cbw;
    root_memzero(&cbw, sizeof(cbw));

    cbw.signature = USB_MASS_CBW_SIGNATURE;
    cbw.tag = ++device->next_tag;
    cbw.transfer_length = data_length;
    cbw.flags = data_in ? 0x80u : 0x00u;
    cbw.lun = 0u;
    cbw.command_length = command_length;
    root_memcpy(cbw.command, command, command_length);

    if (
        !xhci_bulk_transfer(
            device->usb_device_id,
            device->bulk_out_endpoint,
            &cbw,
            sizeof(cbw)
        )
    )
    {
        return false;
    }

    if (data_length > 0u)
    {
        u8 endpoint =
            data_in
            ? device->bulk_in_endpoint
            : device->bulk_out_endpoint;

        if (
            !xhci_bulk_transfer(
                device->usb_device_id,
                endpoint,
                data,
                data_length
            )
        )
        {
            return false;
        }
    }

    UsbMassCsw csw;
    root_memzero(&csw, sizeof(csw));

    if (
        !xhci_bulk_transfer(
            device->usb_device_id,
            device->bulk_in_endpoint,
            &csw,
            sizeof(csw)
        )
    )
    {
        return false;
    }

    return
        csw.signature == USB_MASS_CSW_SIGNATURE
        && csw.tag == cbw.tag
        && csw.status == 0u;
}

static bool mass_test_unit_ready(UsbMassRuntime* device)
{
    u8 command[6];
    root_memzero(command, sizeof(command));
    command[0] = SCSI_TEST_UNIT_READY;

    return mass_bot_command(
        device,
        command,
        sizeof(command),
        NULL,
        0u,
        false
    );
}

static bool mass_inquiry(UsbMassRuntime* device)
{
    u8 command[6];
    root_memzero(command, sizeof(command));
    command[0] = SCSI_INQUIRY;
    command[4] = 36u;

    u8 data[36];
    root_memzero(data, sizeof(data));

    if (
        !mass_bot_command(
            device,
            command,
            sizeof(command),
            data,
            sizeof(data),
            true
        )
    )
    {
        return false;
    }

    copy_trim_ascii(
        device->vendor,
        sizeof(device->vendor),
        data + 8u,
        8u
    );

    copy_trim_ascii(
        device->product,
        sizeof(device->product),
        data + 16u,
        16u
    );

    copy_trim_ascii(
        device->revision,
        sizeof(device->revision),
        data + 32u,
        4u
    );

    return true;
}

static bool mass_read_capacity(UsbMassRuntime* device)
{
    u8 command[10];
    root_memzero(command, sizeof(command));
    command[0] = SCSI_READ_CAPACITY_10;

    u8 data[8];
    root_memzero(data, sizeof(data));

    if (
        !mass_bot_command(
            device,
            command,
            sizeof(command),
            data,
            sizeof(data),
            true
        )
    )
    {
        return false;
    }

    u32 last_lba = be32_read(data);
    u32 block_size = be32_read(data + 4u);

    if (
        block_size == 0u
        || last_lba == 0xFFFFFFFFu
    )
    {
        return false;
    }

    device->block_size = block_size;
    device->block_count = (u64)last_lba + 1u;

    return true;
}

static bool mass_read_blocks(
    RootBlockDevice* block,
    u64 lba,
    u32 count,
    void* buffer
)
{
    if (
        block == NULL
        || block->driver_data == NULL
        || buffer == NULL
        || count == 0u
        || count > USB_MASS_MAX_TRANSFER_BLOCKS
        || lba > 0xFFFFFFFFull
    )
    {
        return false;
    }

    UsbMassRuntime* device =
        (UsbMassRuntime*)block->driver_data;

    if (
        device->block_size == 0u
        || count > 0xFFFFu
    )
    {
        return false;
    }

    u64 byte_count =
        (u64)count
        *
        (u64)device->block_size;

    if (byte_count > 0xFFFFFFFFull)
    {
        return false;
    }

    u8 command[10];
    root_memzero(command, sizeof(command));

    command[0] = SCSI_READ_10;
    be32_write(command + 2u, (u32)lba);
    be16_write(command + 7u, (u16)count);

    return mass_bot_command(
        device,
        command,
        sizeof(command),
        buffer,
        (u32)byte_count,
        true
    );
}

static int mass_alloc(void)
{
    for (usize i = 0u; i < USB_MASS_MAX_DEVICES; i++)
    {
        if (!mass_devices[i].used)
        {
            root_memzero(
                &mass_devices[i],
                sizeof(mass_devices[i])
            );

            mass_devices[i].used = true;
            mass_devices[i].next_tag = 0x524F4F54u;
            return (int)i;
        }
    }

    return -1;
}

static bool mass_find_interface(
    const UsbDeviceInfo* usb,
    u8* interface_number,
    u8* bulk_in,
    u16* bulk_in_packet,
    u8* bulk_out,
    u16* bulk_out_packet
)
{
    if (
        usb == NULL
        || interface_number == NULL
        || bulk_in == NULL
        || bulk_in_packet == NULL
        || bulk_out == NULL
        || bulk_out_packet == NULL
    )
    {
        return false;
    }

    for (u8 i = 0u; i < usb->interface_count; i++)
    {
        const UsbInterfaceInfo* interface = &usb->interfaces[i];

        if (
            interface->class_code != USB_CLASS_MASS_STORAGE
            || interface->protocol != USB_MASS_PROTOCOL_BULK_ONLY
        )
        {
            continue;
        }

        u8 found_in = 0u;
        u8 found_out = 0u;
        u16 packet_in = 0u;
        u16 packet_out = 0u;

        for (u8 e = 0u; e < interface->endpoint_count; e++)
        {
            const UsbEndpointInfo* endpoint =
                &interface->endpoints[e];

            if (
                (endpoint->attributes & USB_ENDPOINT_TRANSFER_MASK)
                != USB_ENDPOINT_TRANSFER_BULK
            )
            {
                continue;
            }

            if (endpoint->address & USB_ENDPOINT_DIRECTION_IN)
            {
                found_in = endpoint->address;
                packet_in = endpoint->max_packet_size;
            }
            else
            {
                found_out = endpoint->address;
                packet_out = endpoint->max_packet_size;
            }
        }

        if (
            found_in != 0u
            && found_out != 0u
            && packet_in != 0u
            && packet_out != 0u
        )
        {
            *interface_number = interface->number;
            *bulk_in = found_in;
            *bulk_in_packet = packet_in;
            *bulk_out = found_out;
            *bulk_out_packet = packet_out;
            return true;
        }
    }

    return false;
}

void usb_mass_storage_init(void)
{
    root_memzero(mass_devices, sizeof(mass_devices));
}

bool usb_mass_storage_scan(void)
{
    block_device_remove_bus(ROOT_BLOCK_BUS_USB);
    root_memzero(mass_devices, sizeof(mass_devices));

    if (!xhci_any_running())
    {
        if (!xhci_start())
        {
            return false;
        }
    }

    bool found_any = false;

    for (usize index = 0u; index < usb_device_count(); index++)
    {
        UsbDeviceInfo usb;

        if (!usb_get_device(index, &usb))
        {
            continue;
        }

        u8 interface_number = 0u;
        u8 bulk_in = 0u;
        u8 bulk_out = 0u;
        u16 bulk_in_packet = 0u;
        u16 bulk_out_packet = 0u;

        if (
            !mass_find_interface(
                &usb,
                &interface_number,
                &bulk_in,
                &bulk_in_packet,
                &bulk_out,
                &bulk_out_packet
            )
        )
        {
            continue;
        }

        int slot = mass_alloc();

        if (slot < 0)
        {
            break;
        }

        UsbMassRuntime* device = &mass_devices[slot];

        device->usb_device_id = usb.id;
        device->interface_number = interface_number;
        device->bulk_in_endpoint = bulk_in;
        device->bulk_out_endpoint = bulk_out;
        device->bulk_in_packet = bulk_in_packet;
        device->bulk_out_packet = bulk_out_packet;

        if (
            !xhci_configure_bulk_endpoints(
                usb.id,
                usb.configuration_value,
                bulk_out,
                bulk_out_packet,
                bulk_in,
                bulk_in_packet
            )
        )
        {
            root_memzero(device, sizeof(*device));
            continue;
        }

        /* Some devices briefly report not-ready after configuration. */
        (void)mass_test_unit_ready(device);

        if (
            !mass_inquiry(device)
            || !mass_read_capacity(device)
        )
        {
            root_memzero(device, sizeof(*device));
            continue;
        }

        RootBlockDevice block;
        root_memzero(&block, sizeof(block));

        block.read_only = true;
        block.bus = ROOT_BLOCK_BUS_USB;
        block.block_size = device->block_size;
        block.block_count = device->block_count;
        block.driver_data = device;
        block.read = mass_read_blocks;
        block.write = NULL;

        root_strlcpy(
            block.name,
            "usb-storage",
            sizeof(block.name)
        );

        if (device->product[0] != '\0')
        {
            root_strlcpy(
                block.model,
                device->product,
                sizeof(block.model)
            );
        }
        else if (usb.product[0] != '\0')
        {
            root_strlcpy(
                block.model,
                usb.product,
                sizeof(block.model)
            );
        }
        else
        {
            root_strlcpy(
                block.model,
                "USB Mass Storage",
                sizeof(block.model)
            );
        }

        if (
            !block_device_register(
                &block,
                &device->block_device_id
            )
        )
        {
            root_memzero(device, sizeof(*device));
            continue;
        }

        found_any = true;
    }

    return found_any;
}

usize usb_mass_storage_count(void)
{
    usize count = 0u;

    for (usize i = 0u; i < USB_MASS_MAX_DEVICES; i++)
    {
        if (mass_devices[i].used)
        {
            count++;
        }
    }

    return count;
}

bool usb_mass_storage_get(
    usize index,
    UsbMassStorageInfo* output
)
{
    if (output == NULL)
    {
        return false;
    }

    usize current = 0u;

    for (usize i = 0u; i < USB_MASS_MAX_DEVICES; i++)
    {
        if (!mass_devices[i].used)
        {
            continue;
        }

        if (current == index)
        {
            const UsbMassRuntime* source = &mass_devices[i];

            root_memzero(output, sizeof(*output));
            output->used = true;
            output->usb_device_id = source->usb_device_id;
            output->block_device_id = source->block_device_id;
            output->interface_number = source->interface_number;
            output->bulk_in_endpoint = source->bulk_in_endpoint;
            output->bulk_out_endpoint = source->bulk_out_endpoint;
            output->block_size = source->block_size;
            output->block_count = source->block_count;

            root_strlcpy(
                output->vendor,
                source->vendor,
                sizeof(output->vendor)
            );
            root_strlcpy(
                output->product,
                source->product,
                sizeof(output->product)
            );
            root_strlcpy(
                output->revision,
                source->revision,
                sizeof(output->revision)
            );

            return true;
        }

        current++;
    }

    return false;
}
