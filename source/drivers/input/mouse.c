#include "mouse.h"

#include "io.h"

#define PS2_DATA_PORT             0x60u
#define PS2_STATUS_PORT           0x64u
#define PS2_COMMAND_PORT          0x64u

#define PS2_STATUS_OUTPUT_FULL    0x01u
#define PS2_STATUS_INPUT_FULL     0x02u
#define PS2_STATUS_AUX_DATA       0x20u

#define PS2_COMMAND_WRITE_MOUSE   0xD4u

#define MOUSE_ACK                 0xFAu
#define MOUSE_SET_DEFAULTS        0xF6u
#define MOUSE_SET_SAMPLE_RATE     0xF3u
#define MOUSE_GET_DEVICE_ID       0xF2u
#define MOUSE_ENABLE_STREAMING    0xF4u

#define MOUSE_DEVICE_STANDARD     0x00u
#define MOUSE_DEVICE_WHEEL        0x03u
#define MOUSE_DEVICE_EXPLORER     0x04u

#define MOUSE_IO_TIMEOUT          200000u

static u8 packet_bytes[4];
static u8 packet_index = 0;
static u8 packet_size = 3;
static bool wheel_available = false;

static bool wait_input_clear(void)
{
    for (u32 i = 0; i < MOUSE_IO_TIMEOUT; i++)
    {
        if ((inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL) == 0)
            return true;

        __asm__ volatile("pause");
    }

    return false;
}

static bool wait_mouse_output(void)
{
    for (u32 i = 0; i < MOUSE_IO_TIMEOUT; i++)
    {
        u8 status = inb(PS2_STATUS_PORT);

        if (
            (status & PS2_STATUS_OUTPUT_FULL) != 0 &&
            (status & PS2_STATUS_AUX_DATA) != 0
        )
        {
            return true;
        }

        __asm__ volatile("pause");
    }

    return false;
}

static bool read_mouse_byte(u8* value)
{
    if (value == NULL || !wait_mouse_output())
        return false;

    *value = inb(PS2_DATA_PORT);
    return true;
}

static bool write_mouse_byte(u8 value)
{
    if (!wait_input_clear())
        return false;

    outb(PS2_COMMAND_PORT, PS2_COMMAND_WRITE_MOUSE);

    if (!wait_input_clear())
        return false;

    outb(PS2_DATA_PORT, value);
    return true;
}

static bool mouse_command(u8 command)
{
    u8 reply = 0;

    if (!write_mouse_byte(command))
        return false;

    if (!read_mouse_byte(&reply))
        return false;

    return reply == MOUSE_ACK;
}

static bool mouse_command_value(
    u8 command,
    u8 value
)
{
    if (!mouse_command(command))
        return false;

    return mouse_command(value);
}

static bool try_enable_wheel(u8* device_id)
{
    if (device_id == NULL)
        return false;

    /* Standard IntelliMouse handshake: 200, 100, 80 samples/sec. */
    if (!mouse_command_value(MOUSE_SET_SAMPLE_RATE, 200u))
        return false;

    if (!mouse_command_value(MOUSE_SET_SAMPLE_RATE, 100u))
        return false;

    if (!mouse_command_value(MOUSE_SET_SAMPLE_RATE, 80u))
        return false;

    if (!mouse_command(MOUSE_GET_DEVICE_ID))
        return false;

    if (!read_mouse_byte(device_id))
        return false;

    return true;
}

bool mouse_init(void)
{
    packet_index = 0;
    packet_size = 3;
    wheel_available = false;

    /* Defaults also disable streaming while configuration is changed. */
    if (!mouse_command(MOUSE_SET_DEFAULTS))
        return false;

    u8 device_id = MOUSE_DEVICE_STANDARD;

    if (try_enable_wheel(&device_id))
    {
        if (
            device_id == MOUSE_DEVICE_WHEEL ||
            device_id == MOUSE_DEVICE_EXPLORER
        )
        {
            packet_size = 4;
            wheel_available = true;
        }
    }

    /* Failure to negotiate wheel support is not fatal: keep 3-byte mode. */
    if (!mouse_command(MOUSE_ENABLE_STREAMING))
        return false;

    return true;
}

bool mouse_feed_byte(
    u8 data,
    MousePacket* packet
)
{
    if (packet == NULL)
        return false;

    /* Byte zero of every PS/2 packet always has bit 3 set. */
    if (packet_index == 0 && (data & 0x08u) == 0)
        return false;

    packet_bytes[packet_index++] = data;

    if (packet_index < packet_size)
        return false;

    packet_index = 0;

    u8 status = packet_bytes[0];

    packet->left = (status & 0x01u) != 0;
    packet->right = (status & 0x02u) != 0;
    packet->middle = (status & 0x04u) != 0;

    /* Ignore movement marked overflow instead of producing a wild cursor jump. */
    if ((status & 0xC0u) != 0)
    {
        packet->dx = 0;
        packet->dy = 0;
    }
    else
    {
        packet->dx = (i32)(i8)packet_bytes[1];

        /* PS/2 positive Y points upward; framebuffer Y points downward. */
        packet->dy = -(i32)(i8)packet_bytes[2];
    }

    packet->wheel = 0;

    if (packet_size == 4)
    {
        if (wheel_available)
        {
            /* QEMU IMPS/2 (ID 3) emits an 8-bit signed wheel delta. */
            packet->wheel = (i32)(i8)packet_bytes[3];
        }
    }

    return true;
}

bool mouse_wheel_available(void)
{
    return wheel_available;
}
