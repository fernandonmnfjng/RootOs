#include "rootdisplay.h"

/* ============================================================
 * FRAMEBUFFER STATE
 * ============================================================ */

static volatile u8* framebuffer = NULL;
static u32 display_width = 0;
static u32 display_height = 0;
static u32 display_pitch = 0;
static u8 display_bpp = 0;
static u8 display_bytes_per_pixel = 0;
static bool display_available = false;

/* Physical framebuffer channel layout. */
static u8 red_position = 16;
static u8 red_size = 8;
static u8 green_position = 8;
static u8 green_size = 8;
static u8 blue_position = 0;
static u8 blue_size = 8;

/* Nested bulk update depth. */
static u32 update_depth = 0;

/* ============================================================
 * SOFTWARE MOUSE CURSOR
 * ============================================================ */

#define ROOT_CURSOR_WIDTH  12u
#define ROOT_CURSOR_HEIGHT 19u

static bool cursor_enabled = false;
static bool cursor_drawn = false;
static i32 cursor_x = 0;
static i32 cursor_y = 0;

static u32 cursor_background[
    ROOT_CURSOR_WIDTH * ROOT_CURSOR_HEIGHT
];

/* B = black border, W = white fill, space = transparent. */
static const char cursor_shape[ROOT_CURSOR_HEIGHT][ROOT_CURSOR_WIDTH + 1] =
{
    "B           ",
    "BB          ",
    "BWB         ",
    "BWWB        ",
    "BWWWB       ",
    "BWWWWB      ",
    "BWWWWWB     ",
    "BWWWWWWB    ",
    "BWWWWWWWB   ",
    "BWWWWBBBBB  ",
    "BWWBWB      ",
    "BWB BWB     ",
    "BB  BWB     ",
    "B    BWB    ",
    "     BWB    ",
    "     BWB    ",
    "      BWB   ",
    "      BWB   ",
    "       BB   "
};

/* ============================================================
 * COLOR CONVERSION
 * ============================================================ */

static u32 channel_mask(u8 bits)
{
    if (bits == 0)
        return 0;

    if (bits >= 32)
        return 0xFFFFFFFFu;

    return (1u << bits) - 1u;
}

static u32 channel_pack(u8 value, u8 bits)
{
    if (bits == 0)
        return 0;

    if (bits == 8)
        return value;

    u32 maximum = channel_mask(bits);
    return (((u32)value * maximum) + 127u) / 255u;
}

static u8 channel_unpack(u32 value, u8 bits)
{
    if (bits == 0)
        return 0;

    if (bits == 8)
        return (u8)value;

    u32 maximum = channel_mask(bits);
    if (maximum == 0)
        return 0;

    return (u8)(((value * 255u) + (maximum / 2u)) / maximum);
}

static bool rgb_layout_valid(void)
{
    if (red_size == 0 || green_size == 0 || blue_size == 0)
        return false;

    if (red_size > 8 || green_size > 8 || blue_size > 8)
        return false;

    if ((u32)red_position + red_size > display_bpp)
        return false;

    if ((u32)green_position + green_size > display_bpp)
        return false;

    if ((u32)blue_position + blue_size > display_bpp)
        return false;

    u32 red_mask = channel_mask(red_size) << red_position;
    u32 green_mask = channel_mask(green_size) << green_position;
    u32 blue_mask = channel_mask(blue_size) << blue_position;

    if ((red_mask & green_mask) != 0)
        return false;

    if ((red_mask & blue_mask) != 0)
        return false;

    if ((green_mask & blue_mask) != 0)
        return false;

    return true;
}

static u32 pack_physical_color(u32 color)
{
    u8 red = (u8)((color >> 16) & 0xFFu);
    u8 green = (u8)((color >> 8) & 0xFFu);
    u8 blue = (u8)(color & 0xFFu);

    u32 packed = 0;
    packed |= channel_pack(red, red_size) << red_position;
    packed |= channel_pack(green, green_size) << green_position;
    packed |= channel_pack(blue, blue_size) << blue_position;
    return packed;
}

static u32 physical_rgb_mask(void)
{
    u32 mask = 0;
    mask |= channel_mask(red_size) << red_position;
    mask |= channel_mask(green_size) << green_position;
    mask |= channel_mask(blue_size) << blue_position;
    return mask;
}

static u32 unpack_physical_color(u32 packed)
{
    u32 red_raw =
        (packed >> red_position) & channel_mask(red_size);

    u32 green_raw =
        (packed >> green_position) & channel_mask(green_size);

    u32 blue_raw =
        (packed >> blue_position) & channel_mask(blue_size);

    return rootdisplay_rgb(
        channel_unpack(red_raw, red_size),
        channel_unpack(green_raw, green_size),
        channel_unpack(blue_raw, blue_size)
    );
}

/* ============================================================
 * RAW PIXEL ACCESS
 * ============================================================ */

static void raw_put_packed_pixel(
    u32 x,
    u32 y,
    u32 packed
)
{
    if (!display_available || x >= display_width || y >= display_height)
        return;

    volatile u8* pixel =
        framebuffer +
        (y * display_pitch) +
        (x * display_bytes_per_pixel);

    pixel[0] = (u8)(packed & 0xFFu);
    pixel[1] = (u8)((packed >> 8) & 0xFFu);
    pixel[2] = (u8)((packed >> 16) & 0xFFu);

    if (display_bpp == 32)
        pixel[3] = (u8)((packed >> 24) & 0xFFu);
}


static void raw_put_pixel(u32 x, u32 y, u32 color)
{
    raw_put_packed_pixel(
        x,
        y,
        pack_physical_color(color)
    );
}

static u32 raw_get_packed_pixel(u32 x, u32 y)
{
    if (!display_available || x >= display_width || y >= display_height)
        return 0;

    volatile u8* pixel =
        framebuffer +
        (y * display_pitch) +
        (x * display_bytes_per_pixel);

    u32 packed = (u32)pixel[0];
    packed |= (u32)pixel[1] << 8;
    packed |= (u32)pixel[2] << 16;

    if (display_bpp == 32)
        packed |= (u32)pixel[3] << 24;

    return packed;
}

static u32 raw_get_pixel(u32 x, u32 y)
{
    return unpack_physical_color(
        raw_get_packed_pixel(x, y)
    );
}

/* ============================================================
 * CURSOR INTERNALS
 * ============================================================ */

static bool point_inside_cursor(
    u32 x,
    u32 y,
    u32* local_x,
    u32* local_y
)
{
    if (!cursor_drawn)
        return false;

    if ((i32)x < cursor_x || (i32)y < cursor_y)
        return false;

    i32 relative_x = (i32)x - cursor_x;
    i32 relative_y = (i32)y - cursor_y;

    if (
        relative_x < 0 ||
        relative_y < 0 ||
        relative_x >= (i32)ROOT_CURSOR_WIDTH ||
        relative_y >= (i32)ROOT_CURSOR_HEIGHT
    )
    {
        return false;
    }

    if (local_x != NULL)
        *local_x = (u32)relative_x;

    if (local_y != NULL)
        *local_y = (u32)relative_y;

    return true;
}

static void cursor_restore_background(void)
{
    if (!cursor_drawn)
        return;

    for (u32 y = 0; y < ROOT_CURSOR_HEIGHT; y++)
    {
        for (u32 x = 0; x < ROOT_CURSOR_WIDTH; x++)
        {
            i32 screen_x = cursor_x + (i32)x;
            i32 screen_y = cursor_y + (i32)y;

            if (
                screen_x < 0 ||
                screen_y < 0 ||
                screen_x >= (i32)display_width ||
                screen_y >= (i32)display_height
            )
            {
                continue;
            }

            u32 index = y * ROOT_CURSOR_WIDTH + x;
            raw_put_pixel(
                (u32)screen_x,
                (u32)screen_y,
                cursor_background[index]
            );
        }
    }

    cursor_drawn = false;
}

static void cursor_capture_background(void)
{
    for (u32 y = 0; y < ROOT_CURSOR_HEIGHT; y++)
    {
        for (u32 x = 0; x < ROOT_CURSOR_WIDTH; x++)
        {
            u32 index = y * ROOT_CURSOR_WIDTH + x;
            i32 screen_x = cursor_x + (i32)x;
            i32 screen_y = cursor_y + (i32)y;

            if (
                screen_x < 0 ||
                screen_y < 0 ||
                screen_x >= (i32)display_width ||
                screen_y >= (i32)display_height
            )
            {
                cursor_background[index] = 0;
                continue;
            }

            cursor_background[index] =
                raw_get_pixel((u32)screen_x, (u32)screen_y);
        }
    }
}

static void cursor_draw(void)
{
    if (!cursor_enabled || !display_available || update_depth != 0)
        return;

    u32 black = rootdisplay_rgb(0, 0, 0);
    u32 white = rootdisplay_rgb(255, 255, 255);

    for (u32 y = 0; y < ROOT_CURSOR_HEIGHT; y++)
    {
        for (u32 x = 0; x < ROOT_CURSOR_WIDTH; x++)
        {
            char shape = cursor_shape[y][x];
            if (shape == ' ')
                continue;

            i32 screen_x = cursor_x + (i32)x;
            i32 screen_y = cursor_y + (i32)y;

            if (
                screen_x < 0 ||
                screen_y < 0 ||
                screen_x >= (i32)display_width ||
                screen_y >= (i32)display_height
            )
            {
                continue;
            }

            raw_put_pixel(
                (u32)screen_x,
                (u32)screen_y,
                shape == 'B' ? black : white
            );
        }
    }

    cursor_drawn = true;
}

/* ============================================================
 * PUBLIC API
 * ============================================================ */

void rootdisplay_init(const MultibootInfo* multiboot)
{
    framebuffer = NULL;
    display_width = 0;
    display_height = 0;
    display_pitch = 0;
    display_bpp = 0;
    display_bytes_per_pixel = 0;
    display_available = false;

    cursor_enabled = false;
    cursor_drawn = false;
    cursor_x = 0;
    cursor_y = 0;
    update_depth = 0;

    if (multiboot == NULL)
        return;

    if ((multiboot->flags & MULTIBOOT_INFO_FRAMEBUFFER) == 0)
        return;

    if (multiboot->framebuffer_type != 1)
        return;

    if (multiboot->framebuffer_addr > 0xFFFFFFFFULL)
        return;

    if (
        multiboot->framebuffer_width == 0 ||
        multiboot->framebuffer_height == 0
    )
    {
        return;
    }

    if (
        multiboot->framebuffer_bpp != 24 &&
        multiboot->framebuffer_bpp != 32
    )
    {
        return;
    }

    framebuffer =
        (volatile u8*)(usize)multiboot->framebuffer_addr;

    display_width = multiboot->framebuffer_width;
    display_height = multiboot->framebuffer_height;
    display_pitch = multiboot->framebuffer_pitch;
    display_bpp = multiboot->framebuffer_bpp;
    display_bytes_per_pixel = display_bpp / 8;

    if (
        display_pitch <
        display_width * display_bytes_per_pixel
    )
    {
        framebuffer = NULL;
        return;
    }

    red_position = multiboot->framebuffer_red_field_position;
    red_size = multiboot->framebuffer_red_mask_size;
    green_position = multiboot->framebuffer_green_field_position;
    green_size = multiboot->framebuffer_green_mask_size;
    blue_position = multiboot->framebuffer_blue_field_position;
    blue_size = multiboot->framebuffer_blue_mask_size;

    /*
     * Defensive fallback. QEMU std normally provides 8:8:8 RGB.
     * If the Multiboot RGB metadata is malformed, never let one
     * missing channel turn white into yellow/red.
     */
    if (!rgb_layout_valid())
    {
        red_position = 16;
        red_size = 8;
        green_position = 8;
        green_size = 8;
        blue_position = 0;
        blue_size = 8;
    }

    display_available = true;
}

bool rootdisplay_ready(void)
{
    return display_available;
}

u32 rootdisplay_width(void)
{
    return display_width;
}

u32 rootdisplay_height(void)
{
    return display_height;
}

u32 rootdisplay_rgb(u8 red, u8 green, u8 blue)
{
    return
        ((u32)red << 16) |
        ((u32)green << 8) |
        (u32)blue;
}

void rootdisplay_begin_update(void)
{
    if (!display_available)
        return;

    if (update_depth == 0 && cursor_drawn)
        cursor_restore_background();

    update_depth++;
}

void rootdisplay_end_update(void)
{
    if (!display_available || update_depth == 0)
        return;

    update_depth--;

    if (update_depth == 0 && cursor_enabled)
    {
        cursor_capture_background();
        cursor_draw();
    }
}

void rootdisplay_put_pixel(u32 x, u32 y, u32 color)
{
    if (!display_available || x >= display_width || y >= display_height)
        return;

    if (update_depth != 0)
    {
        raw_put_pixel(x, y, color);
        return;
    }

    u32 local_x;
    u32 local_y;

    if (point_inside_cursor(x, y, &local_x, &local_y))
    {
        u32 index = local_y * ROOT_CURSOR_WIDTH + local_x;
        cursor_background[index] = color;

        if (cursor_shape[local_y][local_x] == ' ')
            raw_put_pixel(x, y, color);

        return;
    }

    raw_put_pixel(x, y, color);
}

u32 rootdisplay_get_pixel(u32 x, u32 y)
{
    if (!display_available || x >= display_width || y >= display_height)
        return 0;

    u32 local_x;
    u32 local_y;

    if (point_inside_cursor(x, y, &local_x, &local_y))
    {
        return cursor_background[
            local_y * ROOT_CURSOR_WIDTH + local_x
        ];
    }

    return raw_get_pixel(x, y);
}

void rootdisplay_draw_mono_bitmap(
    u32 x,
    u32 y,
    u32 width,
    u32 height,
    const u8* bitmap,
    u32 stride_bytes,
    u32 foreground,
    u32 background,
    bool opaque
)
{
    if (
        !display_available ||
        bitmap == NULL ||
        width == 0 ||
        height == 0 ||
        stride_bytes == 0 ||
        x >= display_width ||
        y >= display_height
    )
    {
        return;
    }

    u32 draw_width = width;
    u32 draw_height = height;

    if (draw_width > display_width - x)
        draw_width = display_width - x;

    if (draw_height > display_height - y)
        draw_height = display_height - y;

    u32 foreground_packed = pack_physical_color(foreground);
    u32 background_packed = pack_physical_color(background);

    rootdisplay_begin_update();

    for (u32 py = 0; py < draw_height; py++)
    {
        const u8* row = bitmap + (usize)py * stride_bytes;

        for (u32 px = 0; px < draw_width; px++)
        {
            u8 byte = row[px / 8u];
            u8 bit = (u8)(7u - (px % 8u));
            bool set = (byte & (u8)(1u << bit)) != 0;

            if (set)
            {
                raw_put_packed_pixel(
                    x + px,
                    y + py,
                    foreground_packed
                );
            }
            else if (opaque)
            {
                raw_put_packed_pixel(
                    x + px,
                    y + py,
                    background_packed
                );
            }
        }
    }

    rootdisplay_end_update();
}

void rootdisplay_fill_rect(
    u32 x,
    u32 y,
    u32 width,
    u32 height,
    u32 color
)
{
    if (
        !display_available ||
        width == 0 ||
        height == 0 ||
        x >= display_width ||
        y >= display_height
    )
    {
        return;
    }

    u32 end_x = x + width;
    u32 end_y = y + height;

    if (end_x < x || end_x > display_width)
        end_x = display_width;

    if (end_y < y || end_y > display_height)
        end_y = display_height;

    rootdisplay_begin_update();

    u32 packed = pack_physical_color(color);

    for (u32 py = y; py < end_y; py++)
    {
        for (u32 px = x; px < end_x; px++)
            raw_put_packed_pixel(px, py, packed);
    }

    rootdisplay_end_update();
}

void rootdisplay_invert_rect(
    u32 x,
    u32 y,
    u32 width,
    u32 height
)
{
    if (
        !display_available ||
        width == 0 ||
        height == 0 ||
        x >= display_width ||
        y >= display_height
    )
    {
        return;
    }

    u32 end_x = x + width;
    u32 end_y = y + height;

    if (end_x < x || end_x > display_width)
        end_x = display_width;

    if (end_y < y || end_y > display_height)
        end_y = display_height;

    /*
     * Selection highlighting is hot-path rendering. Invert the
     * framebuffer's packed RGB bits directly; do not unpack and
     * repack every pixel.
     */
    u32 mask = physical_rgb_mask();

    rootdisplay_begin_update();

    for (u32 py = y; py < end_y; py++)
    {
        for (u32 px = x; px < end_x; px++)
        {
            u32 packed = raw_get_packed_pixel(px, py);
            raw_put_packed_pixel(px, py, packed ^ mask);
        }
    }

    rootdisplay_end_update();
}

static void framebuffer_copy_forward(
    volatile u8* destination,
    const volatile u8* source,
    usize bytes
)
{
    /*
     * Hot framebuffer path. Copy cache-line sized chunks with explicit
     * 32-bit stores instead of byte-at-a-time MMIO writes. The loop is
     * manually unrolled so freestanding -O2 does not need libc or SIMD.
     */
    if (((((usize)destination) | ((usize)source)) & 3u) == 0)
    {
        volatile u32* dst32 = (volatile u32*)destination;
        const volatile u32* src32 = (const volatile u32*)source;
        usize words = bytes / 4u;

        while (words >= 8u)
        {
            dst32[0] = src32[0];
            dst32[1] = src32[1];
            dst32[2] = src32[2];
            dst32[3] = src32[3];
            dst32[4] = src32[4];
            dst32[5] = src32[5];
            dst32[6] = src32[6];
            dst32[7] = src32[7];
            dst32 += 8;
            src32 += 8;
            words -= 8u;
        }

        while (words != 0u)
        {
            *dst32++ = *src32++;
            words--;
        }

        usize copied = (bytes / 4u) * 4u;
        destination += copied;
        source += copied;
        bytes -= copied;
    }

    while (bytes != 0u)
    {
        *destination++ = *source++;
        bytes--;
    }
}

static void framebuffer_copy_backward(
    volatile u8* destination,
    const volatile u8* source,
    usize bytes
)
{
    if (bytes == 0u)
        return;

    if (((((usize)destination) | ((usize)source) | bytes) & 3u) == 0)
    {
        volatile u32* dst32 =
            (volatile u32*)(destination + bytes);
        const volatile u32* src32 =
            (const volatile u32*)(source + bytes);
        usize words = bytes / 4u;

        while (words >= 8u)
        {
            dst32[-1] = src32[-1];
            dst32[-2] = src32[-2];
            dst32[-3] = src32[-3];
            dst32[-4] = src32[-4];
            dst32[-5] = src32[-5];
            dst32[-6] = src32[-6];
            dst32[-7] = src32[-7];
            dst32[-8] = src32[-8];
            dst32 -= 8;
            src32 -= 8;
            words -= 8u;
        }

        while (words != 0u)
        {
            --dst32;
            --src32;
            *dst32 = *src32;
            words--;
        }

        return;
    }

    while (bytes != 0u)
    {
        bytes--;
        destination[bytes] = source[bytes];
    }
}

static void framebuffer_fill_rows(
    u32 start_y,
    u32 row_count,
    u32 packed
)
{
    if (row_count == 0u || start_y >= display_height)
        return;

    if (row_count > display_height - start_y)
        row_count = display_height - start_y;

    if (display_bytes_per_pixel == 4u)
    {
        for (u32 row = 0; row < row_count; row++)
        {
            volatile u32* dst =
                (volatile u32*)(framebuffer +
                    (usize)(start_y + row) * display_pitch);
            u32 remaining = display_width;

            while (remaining >= 8u)
            {
                dst[0] = packed;
                dst[1] = packed;
                dst[2] = packed;
                dst[3] = packed;
                dst[4] = packed;
                dst[5] = packed;
                dst[6] = packed;
                dst[7] = packed;
                dst += 8;
                remaining -= 8u;
            }

            while (remaining != 0u)
            {
                *dst++ = packed;
                remaining--;
            }
        }

        return;
    }

    for (u32 y = start_y; y < start_y + row_count; y++)
    {
        for (u32 x = 0; x < display_width; x++)
            raw_put_packed_pixel(x, y, packed);
    }
}

void rootdisplay_scroll_up(
    u32 pixel_rows,
    u32 fill_color
)
{
    if (!display_available || pixel_rows == 0)
        return;

    if (pixel_rows >= display_height)
    {
        rootdisplay_clear(fill_color);
        return;
    }

    rootdisplay_begin_update();

    usize source_offset =
        (usize)pixel_rows * (usize)display_pitch;

    usize bytes_to_move =
        ((usize)display_height - pixel_rows) *
        (usize)display_pitch;

    framebuffer_copy_forward(
        framebuffer,
        framebuffer + source_offset,
        bytes_to_move
    );

    u32 start_y = display_height - pixel_rows;
    u32 packed_fill = pack_physical_color(fill_color);

    for (u32 y = start_y; y < display_height; y++)
    {
        for (u32 x = 0; x < display_width; x++)
            raw_put_packed_pixel(x, y, packed_fill);
    }

    rootdisplay_end_update();
}


void rootdisplay_shift_vertical(
    u32 region_y,
    u32 region_height,
    i32 pixel_delta,
    u32 fill_color
)
{
    if (
        !display_available ||
        region_height == 0u ||
        pixel_delta == 0 ||
        region_y >= display_height
    )
    {
        return;
    }

    if (region_height > display_height - region_y)
        region_height = display_height - region_y;

    u32 amount =
        (u32)(pixel_delta < 0 ? -pixel_delta : pixel_delta);

    if (amount >= region_height)
    {
        rootdisplay_fill_rect(
            0,
            region_y,
            display_width,
            region_height,
            fill_color
        );
        return;
    }

    rootdisplay_begin_update();

    usize pitch = (usize)display_pitch;
    usize bytes_to_move =
        (usize)(region_height - amount) * pitch;

    if (pixel_delta < 0)
    {
        /* Content moves upward. */
        volatile u8* destination =
            framebuffer + (usize)region_y * pitch;
        const volatile u8* source =
            framebuffer + (usize)(region_y + amount) * pitch;

        framebuffer_copy_forward(
            destination,
            source,
            bytes_to_move
        );

        framebuffer_fill_rows(
            region_y + region_height - amount,
            amount,
            pack_physical_color(fill_color)
        );
    }
    else
    {
        /* Content moves downward. Overlap requires a backwards copy. */
        volatile u8* destination =
            framebuffer + (usize)(region_y + amount) * pitch;
        const volatile u8* source =
            framebuffer + (usize)region_y * pitch;

        framebuffer_copy_backward(
            destination,
            source,
            bytes_to_move
        );

        framebuffer_fill_rows(
            region_y,
            amount,
            pack_physical_color(fill_color)
        );
    }

    rootdisplay_end_update();
}

void rootdisplay_clear(u32 color)
{
    rootdisplay_fill_rect(
        0,
        0,
        display_width,
        display_height,
        color
    );
}

void rootdisplay_cursor_enable(bool enabled)
{
    if (!display_available)
    {
        cursor_enabled = false;
        cursor_drawn = false;
        return;
    }

    if (!enabled)
    {
        if (cursor_drawn)
            cursor_restore_background();

        cursor_enabled = false;
        return;
    }

    if (cursor_enabled)
        return;

    cursor_enabled = true;

    if (update_depth == 0)
    {
        cursor_capture_background();
        cursor_draw();
    }
}

void rootdisplay_cursor_move(i32 x, i32 y)
{
    if (!display_available)
        return;

    if (x < 0)
        x = 0;

    if (y < 0)
        y = 0;

    if (x >= (i32)display_width)
        x = (i32)display_width - 1;

    if (y >= (i32)display_height)
        y = (i32)display_height - 1;

    if (x == cursor_x && y == cursor_y)
        return;

    if (cursor_drawn)
        cursor_restore_background();

    cursor_x = x;
    cursor_y = y;

    if (cursor_enabled && update_depth == 0)
    {
        cursor_capture_background();
        cursor_draw();
    }
}

bool rootdisplay_cursor_visible(void)
{
    return cursor_enabled && cursor_drawn;
}
