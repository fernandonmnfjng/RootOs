#include "rootdisplay.h"


typedef struct
{
    bool ready;

    volatile u8* framebuffer;

    u32 width;
    u32 height;

    u32 pitch;

    u8 bpp;

    u8 red_position;
    u8 red_size;

    u8 green_position;
    u8 green_size;

    u8 blue_position;
    u8 blue_size;

} RootDisplayState;


static RootDisplayState display;


/*
 * ============================================================
 * CONVERTIR CANAL RGB
 * ============================================================
 */

static u32 scale_channel(
    u8 value,
    u8 mask_size,
    u8 position
)
{
    if (
        mask_size == 0
        ||
        mask_size >= 32
    )
    {
        return 0;
    }


    u64 maximum =
        (
            ((u64)1 << mask_size)
            -
            1
        );


    u64 scaled =
        (
            (u64)value
            *
            maximum
            +
            127
        )
        /
        255;


    return
        (u32)(
            scaled
            <<
            position
        );
}


/*
 * ============================================================
 * INICIALIZACION
 * ============================================================
 */

bool rootdisplay_init(
    const MultibootInfo* multiboot
)
{
    display.ready =
        false;


    if (
        multiboot == NULL
    )
    {
        return false;
    }


    /*
     * ¿GRUB entregó framebuffer?
     */
    if (
        (
            multiboot->flags
            &
            MULTIBOOT_INFO_FRAMEBUFFER
        )
        ==
        0
    )
    {
        return false;
    }


    /*
     * Queremos RGB directo.
     *
     * framebuffer_type:
     *
     * 0 = indexed
     * 1 = RGB
     * 2 = EGA text
     */
    if (
        multiboot->framebuffer_type
        !=
        1
    )
    {
        return false;
    }


    /*
     * Por ahora soportamos:
     *
     * RGB24
     * RGB32
     */
    if (
        multiboot->framebuffer_bpp
            !=
            24
        &&
        multiboot->framebuffer_bpp
            !=
            32
    )
    {
        return false;
    }


    /*
     * Seguimos siendo un kernel de 32 bits.
     *
     * Si el framebuffer está sobre 4 GiB,
     * todavía no podemos direccionarlo.
     */
    if (
        (
            multiboot->framebuffer_addr
            >>
            32
        )
        !=
        0
    )
    {
        return false;
    }


    if (
        multiboot->framebuffer_width == 0
        ||
        multiboot->framebuffer_height == 0
        ||
        multiboot->framebuffer_pitch == 0
    )
    {
        return false;
    }


    display.framebuffer =
        (volatile u8*)(
            usize
        )(
            u32
        )
        multiboot->framebuffer_addr;


    display.width =
        multiboot->framebuffer_width;

    display.height =
        multiboot->framebuffer_height;

    display.pitch =
        multiboot->framebuffer_pitch;

    display.bpp =
        multiboot->framebuffer_bpp;


    display.red_position =
        multiboot
            ->
        framebuffer_red_field_position;

    display.red_size =
        multiboot
            ->
        framebuffer_red_mask_size;


    display.green_position =
        multiboot
            ->
        framebuffer_green_field_position;

    display.green_size =
        multiboot
            ->
        framebuffer_green_mask_size;


    display.blue_position =
        multiboot
            ->
        framebuffer_blue_field_position;

    display.blue_size =
        multiboot
            ->
        framebuffer_blue_mask_size;


    display.ready =
        true;


    return true;
}


bool rootdisplay_ready(void)
{
    return display.ready;
}


u32 rootdisplay_width(void)
{
    return display.width;
}


u32 rootdisplay_height(void)
{
    return display.height;
}


/*
 * ============================================================
 * RGB
 * ============================================================
 */

u32 rootdisplay_rgb(
    u8 red,
    u8 green,
    u8 blue
)
{
    if (!display.ready)
    {
        return 0;
    }


    return
        scale_channel(
            red,
            display.red_size,
            display.red_position
        )
        |
        scale_channel(
            green,
            display.green_size,
            display.green_position
        )
        |
        scale_channel(
            blue,
            display.blue_size,
            display.blue_position
        );
}


/*
 * ============================================================
 * PIXEL
 * ============================================================
 */

void rootdisplay_put_pixel(
    u32 x,
    u32 y,
    u32 color
)
{
    if (!display.ready)
    {
        return;
    }


    if (
        x >= display.width
        ||
        y >= display.height
    )
    {
        return;
    }


    u32 bytes_per_pixel =
        display.bpp / 8;


    usize offset =
        (
            usize
        )
        y
        *
        display.pitch
        +
        (
            usize
        )
        x
        *
        bytes_per_pixel;


    volatile u8* pixel =
        display.framebuffer
        +
        offset;


    /*
     * 32 bits.
     */
    if (
        bytes_per_pixel == 4
    )
    {
        *(
            volatile u32*
        )pixel =
            color;

        return;
    }


    /*
     * 24 bits.
     *
     * x86 es little-endian.
     */
    if (
        bytes_per_pixel == 3
    )
    {
        pixel[0] =
            (u8)(
                color
                &
                0xFF
            );

        pixel[1] =
            (u8)(
                (
                    color
                    >>
                    8
                )
                &
                0xFF
            );

        pixel[2] =
            (u8)(
                (
                    color
                    >>
                    16
                )
                &
                0xFF
            );
    }
}


/*
 * ============================================================
 * RECTANGULO
 * ============================================================
 */

void rootdisplay_fill_rect(
    u32 x,
    u32 y,
    u32 width,
    u32 height,
    u32 color
)
{
    if (!display.ready)
    {
        return;
    }


    if (
        x >= display.width
        ||
        y >= display.height
    )
    {
        return;
    }


    u32 end_x =
        x + width;

    u32 end_y =
        y + height;


    if (
        end_x > display.width
    )
    {
        end_x =
            display.width;
    }


    if (
        end_y > display.height
    )
    {
        end_y =
            display.height;
    }


    for (
        u32 py = y;
        py < end_y;
        py++
    )
    {
        for (
            u32 px = x;
            px < end_x;
            px++
        )
        {
            rootdisplay_put_pixel(
                px,
                py,
                color
            );
        }
    }
}


void rootdisplay_clear(
    u32 color
)
{
    rootdisplay_fill_rect(
        0,
        0,
        display.width,
        display.height,
        color
    );
}
