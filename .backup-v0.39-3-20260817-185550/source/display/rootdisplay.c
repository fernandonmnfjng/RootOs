#include "rootdisplay.h"


/*
 * ============================================================
 * FRAMEBUFFER
 * ============================================================
 */

static volatile u8* framebuffer =
    NULL;


static u32 display_width =
    0;


static u32 display_height =
    0;


static u32 display_pitch =
    0;


static u8 display_bpp =
    0;


static u8 display_bytes_per_pixel =
    0;


static bool display_available =
    false;


/*
 * ============================================================
 * RGB FORMAT
 * ============================================================
 */

static u8 red_position =
    16;


static u8 red_size =
    8;


static u8 green_position =
    8;


static u8 green_size =
    8;


static u8 blue_position =
    0;


static u8 blue_size =
    8;


/*
 * ============================================================
 * UPDATE BATCH
 * ============================================================
 */

static u32 update_depth =
    0;


/*
 * ============================================================
 * SOFTWARE CURSOR
 * ============================================================
 */

#define ROOT_CURSOR_WIDTH  12
#define ROOT_CURSOR_HEIGHT 19


static bool cursor_enabled =
    false;


static bool cursor_drawn =
    false;


static i32 cursor_x =
    0;


static i32 cursor_y =
    0;


static u32 cursor_background[
    ROOT_CURSOR_WIDTH
    *
    ROOT_CURSOR_HEIGHT
];


/*
 * B = borde negro
 * Y = amarillo
 * espacio = transparente
 */

static const char cursor_shape[
    ROOT_CURSOR_HEIGHT
][
    ROOT_CURSOR_WIDTH + 1
] =
{
    "B           ",
    "BB          ",
    "BYB         ",
    "BYYB        ",
    "BYYYB       ",
    "BYYYYB      ",
    "BYYYYYB     ",
    "BYYYYYYB    ",
    "BYYYYYYYB   ",
    "BYYYYBBBBB  ",
    "BYYBYB      ",
    "BYB BYB     ",
    "BB  BYB     ",
    "B    BYB    ",
    "     BYB    ",
    "     BYB    ",
    "      BYB   ",
    "      BYB   ",
    "       BB   "
};


/*
 * ============================================================
 * RAW PIXEL
 * ============================================================
 */

static void raw_put_pixel(
    u32 x,
    u32 y,
    u32 color
)
{
    if (
        !display_available
        ||
        x >= display_width
        ||
        y >= display_height
    )
    {
        return;
    }


    volatile u8* pixel =
        framebuffer
        +
        (
            y
            *
            display_pitch
        )
        +
        (
            x
            *
            display_bytes_per_pixel
        );


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


    if (
        display_bpp == 32
    )
    {
        pixel[3] =
            (u8)(
                (
                    color
                    >>
                    24
                )
                &
                0xFF
            );
    }
}


static u32 raw_get_pixel(
    u32 x,
    u32 y
)
{
    if (
        !display_available
        ||
        x >= display_width
        ||
        y >= display_height
    )
    {
        return 0;
    }


    volatile u8* pixel =
        framebuffer
        +
        (
            y
            *
            display_pitch
        )
        +
        (
            x
            *
            display_bytes_per_pixel
        );


    u32 color =
        (u32)pixel[0];


    color |=
        (
            (u32)pixel[1]
            <<
            8
        );


    color |=
        (
            (u32)pixel[2]
            <<
            16
        );


    if (
        display_bpp == 32
    )
    {
        color |=
            (
                (u32)pixel[3]
                <<
                24
            );
    }


    return color;
}


/*
 * ============================================================
 * CHANNEL SCALE
 * ============================================================
 */

static u32 scale_channel(
    u8 value,
    u8 bits
)
{
    if (
        bits == 0
    )
    {
        return 0;
    }


    if (
        bits >= 8
    )
    {
        return value;
    }


    u32 maximum =
        (
            1u
            <<
            bits
        )
        -
        1u;


    return
        (
            (
                (u32)value
                *
                maximum
            )
            +
            127u
        )
        /
        255u;
}


/*
 * ============================================================
 * CURSOR HIT TEST
 * ============================================================
 */

static bool point_inside_cursor(
    u32 x,
    u32 y,
    u32* local_x,
    u32* local_y
)
{
    if (
        !cursor_drawn
    )
    {
        return false;
    }


    /*
     * Fast rejection.
     */

    if (
        (i32)x < cursor_x
        ||
        (i32)y < cursor_y
        ||
        (i32)x >=
            cursor_x
            +
            ROOT_CURSOR_WIDTH
        ||
        (i32)y >=
            cursor_y
            +
            ROOT_CURSOR_HEIGHT
    )
    {
        return false;
    }


    u32 rx =
        (u32)(
            (i32)x
            -
            cursor_x
        );


    u32 ry =
        (u32)(
            (i32)y
            -
            cursor_y
        );


    if (
        local_x != NULL
    )
    {
        *local_x =
            rx;
    }


    if (
        local_y != NULL
    )
    {
        *local_y =
            ry;
    }


    return true;
}


/*
 * ============================================================
 * RESTORE CURSOR BACKGROUND
 * ============================================================
 */

static void cursor_restore_background(void)
{
    if (
        !cursor_drawn
    )
    {
        return;
    }


    for (
        u32 y = 0;
        y < ROOT_CURSOR_HEIGHT;
        y++
    )
    {
        for (
            u32 x = 0;
            x < ROOT_CURSOR_WIDTH;
            x++
        )
        {
            i32 sx =
                cursor_x
                +
                (i32)x;


            i32 sy =
                cursor_y
                +
                (i32)y;


            if (
                sx < 0
                ||
                sy < 0
                ||
                sx >= (i32)display_width
                ||
                sy >= (i32)display_height
            )
            {
                continue;
            }


            u32 index =
                (
                    y
                    *
                    ROOT_CURSOR_WIDTH
                )
                +
                x;


            raw_put_pixel(
                (u32)sx,
                (u32)sy,
                cursor_background[
                    index
                ]
            );
        }
    }


    cursor_drawn =
        false;
}


/*
 * ============================================================
 * CAPTURE CURSOR BACKGROUND
 * ============================================================
 */

static void cursor_capture_background(void)
{
    for (
        u32 y = 0;
        y < ROOT_CURSOR_HEIGHT;
        y++
    )
    {
        for (
            u32 x = 0;
            x < ROOT_CURSOR_WIDTH;
            x++
        )
        {
            u32 index =
                (
                    y
                    *
                    ROOT_CURSOR_WIDTH
                )
                +
                x;


            i32 sx =
                cursor_x
                +
                (i32)x;


            i32 sy =
                cursor_y
                +
                (i32)y;


            if (
                sx < 0
                ||
                sy < 0
                ||
                sx >= (i32)display_width
                ||
                sy >= (i32)display_height
            )
            {
                cursor_background[
                    index
                ] =
                    0;


                continue;
            }


            cursor_background[
                index
            ] =
                raw_get_pixel(
                    (u32)sx,
                    (u32)sy
                );
        }
    }
}


/*
 * ============================================================
 * DRAW CURSOR
 * ============================================================
 */

static void cursor_draw(void)
{
    if (
        !cursor_enabled
        ||
        !display_available
        ||
        update_depth != 0
    )
    {
        return;
    }


    u32 black =
        rootdisplay_rgb(
            0,
            0,
            0
        );


    /*
     * Mismo concepto de color que el texto:
     * amarillo.
     */

    u32 yellow =
        rootdisplay_rgb(
            255,
            255,
            0
        );


    for (
        u32 y = 0;
        y < ROOT_CURSOR_HEIGHT;
        y++
    )
    {
        for (
            u32 x = 0;
            x < ROOT_CURSOR_WIDTH;
            x++
        )
        {
            char shape =
                cursor_shape[y][x];


            if (
                shape == ' '
            )
            {
                continue;
            }


            i32 sx =
                cursor_x
                +
                (i32)x;


            i32 sy =
                cursor_y
                +
                (i32)y;


            if (
                sx < 0
                ||
                sy < 0
                ||
                sx >= (i32)display_width
                ||
                sy >= (i32)display_height
            )
            {
                continue;
            }


            raw_put_pixel(
                (u32)sx,
                (u32)sy,
                shape == 'B'
                    ?
                    black
                    :
                    yellow
            );
        }
    }


    cursor_drawn =
        true;
}


/*
 * ============================================================
 * INITIALIZATION
 * ============================================================
 */

void rootdisplay_init(
    const MultibootInfo* multiboot
)
{
    framebuffer =
        NULL;


    display_available =
        false;


    display_width =
        0;


    display_height =
        0;


    display_pitch =
        0;


    display_bpp =
        0;


    display_bytes_per_pixel =
        0;


    cursor_enabled =
        false;


    cursor_drawn =
        false;


    update_depth =
        0;


    if (
        multiboot == NULL
    )
    {
        return;
    }


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
        return;
    }


    if (
        multiboot->framebuffer_type
        !=
        1
    )
    {
        return;
    }


    if (
        multiboot->framebuffer_addr
        >
        0xFFFFFFFFULL
    )
    {
        return;
    }


    if (
        multiboot->framebuffer_width == 0
        ||
        multiboot->framebuffer_height == 0
    )
    {
        return;
    }


    if (
        multiboot->framebuffer_bpp != 24
        &&
        multiboot->framebuffer_bpp != 32
    )
    {
        return;
    }


    framebuffer =
        (
            volatile u8*
        )(
            usize
        )
        multiboot->framebuffer_addr;


    display_width =
        multiboot->framebuffer_width;


    display_height =
        multiboot->framebuffer_height;


    display_pitch =
        multiboot->framebuffer_pitch;


    display_bpp =
        multiboot->framebuffer_bpp;


    display_bytes_per_pixel =
        display_bpp
        /
        8;


    if (
        display_pitch
        <
        display_width
        *
        display_bytes_per_pixel
    )
    {
        framebuffer =
            NULL;


        return;
    }


    red_position =
        multiboot->
        framebuffer_red_field_position;


    red_size =
        multiboot->
        framebuffer_red_mask_size;


    green_position =
        multiboot->
        framebuffer_green_field_position;


    green_size =
        multiboot->
        framebuffer_green_mask_size;


    blue_position =
        multiboot->
        framebuffer_blue_field_position;


    blue_size =
        multiboot->
        framebuffer_blue_mask_size;


    display_available =
        true;
}


/*
 * ============================================================
 * READY
 * ============================================================
 */

bool rootdisplay_ready(void)
{
    return
        display_available;
}


/*
 * ============================================================
 * SIZE
 * ============================================================
 */

u32 rootdisplay_width(void)
{
    return
        display_width;
}


u32 rootdisplay_height(void)
{
    return
        display_height;
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
    u32 result =
        0;


    result |=
        scale_channel(
            red,
            red_size
        )
        <<
        red_position;


    result |=
        scale_channel(
            green,
            green_size
        )
        <<
        green_position;


    result |=
        scale_channel(
            blue,
            blue_size
        )
        <<
        blue_position;


    return result;
}


/*
 * ============================================================
 * BEGIN UPDATE
 * ============================================================
 */

void rootdisplay_begin_update(void)
{
    if (
        !display_available
    )
    {
        return;
    }


    if (
        update_depth == 0
        &&
        cursor_drawn
    )
    {
        cursor_restore_background();
    }


    update_depth++;
}


/*
 * ============================================================
 * END UPDATE
 * ============================================================
 */

void rootdisplay_end_update(void)
{
    if (
        !display_available
        ||
        update_depth == 0
    )
    {
        return;
    }


    update_depth--;


    if (
        update_depth == 0
        &&
        cursor_enabled
    )
    {
        cursor_capture_background();

        cursor_draw();
    }
}


/*
 * ============================================================
 * PUT PIXEL
 * ============================================================
 */

void rootdisplay_put_pixel(
    u32 x,
    u32 y,
    u32 color
)
{
    if (
        !display_available
        ||
        x >= display_width
        ||
        y >= display_height
    )
    {
        return;
    }


    /*
     * Durante una actualización masiva
     * el cursor ya está oculto.
     */

    if (
        update_depth != 0
    )
    {
        raw_put_pixel(
            x,
            y,
            color
        );


        return;
    }


    u32 local_x;

    u32 local_y;


    if (
        point_inside_cursor(
            x,
            y,
            &local_x,
            &local_y
        )
    )
    {
        u32 index =
            (
                local_y
                *
                ROOT_CURSOR_WIDTH
            )
            +
            local_x;


        cursor_background[
            index
        ] =
            color;


        if (
            cursor_shape[
                local_y
            ][
                local_x
            ]
            ==
            ' '
        )
        {
            raw_put_pixel(
                x,
                y,
                color
            );
        }


        return;
    }


    raw_put_pixel(
        x,
        y,
        color
    );
}


/*
 * ============================================================
 * GET PIXEL
 * ============================================================
 */

u32 rootdisplay_get_pixel(
    u32 x,
    u32 y
)
{
    if (
        !display_available
        ||
        x >= display_width
        ||
        y >= display_height
    )
    {
        return 0;
    }


    u32 local_x;

    u32 local_y;


    if (
        point_inside_cursor(
            x,
            y,
            &local_x,
            &local_y
        )
    )
    {
        return
            cursor_background[
                (
                    local_y
                    *
                    ROOT_CURSOR_WIDTH
                )
                +
                local_x
            ];
    }


    return
        raw_get_pixel(
            x,
            y
        );
}


/*
 * ============================================================
 * FILL RECT
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
    if (
        !display_available
        ||
        width == 0
        ||
        height == 0
        ||
        x >= display_width
        ||
        y >= display_height
    )
    {
        return;
    }


    u32 end_x =
        x + width;


    u32 end_y =
        y + height;


    if (
        end_x < x
        ||
        end_x > display_width
    )
    {
        end_x =
            display_width;
    }


    if (
        end_y < y
        ||
        end_y > display_height
    )
    {
        end_y =
            display_height;
    }


    rootdisplay_begin_update();


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
            raw_put_pixel(
                px,
                py,
                color
            );
        }
    }


    rootdisplay_end_update();
}


/*
 * ============================================================
 * INVERT RECT
 * ============================================================
 */

void rootdisplay_invert_rect(
    u32 x,
    u32 y,
    u32 width,
    u32 height
)
{
    if (
        !display_available
        ||
        width == 0
        ||
        height == 0
        ||
        x >= display_width
        ||
        y >= display_height
    )
    {
        return;
    }


    u32 end_x =
        x + width;


    u32 end_y =
        y + height;


    if (
        end_x < x
        ||
        end_x > display_width
    )
    {
        end_x =
            display_width;
    }


    if (
        end_y < y
        ||
        end_y > display_height
    )
    {
        end_y =
            display_height;
    }


    rootdisplay_begin_update();


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
            u32 color =
                raw_get_pixel(
                    px,
                    py
                );


            /*
             * Invertir RGB y mantener byte alto.
             */

            u32 inverted =
                (
                    color
                    &
                    0xFF000000u
                )
                |
                (
                    (
                        color
                        ^
                        0x00FFFFFFu
                    )
                    &
                    0x00FFFFFFu
                );


            raw_put_pixel(
                px,
                py,
                inverted
            );
        }
    }


    rootdisplay_end_update();
}


/*
 * ============================================================
 * CLEAR
 * ============================================================
 */

void rootdisplay_clear(
    u32 color
)
{
    rootdisplay_fill_rect(
        0,
        0,
        display_width,
        display_height,
        color
    );
}


/*
 * ============================================================
 * CURSOR ENABLE
 * ============================================================
 */

void rootdisplay_cursor_enable(
    bool enabled
)
{
    if (
        !display_available
    )
    {
        cursor_enabled =
            false;


        cursor_drawn =
            false;


        return;
    }


    if (
        !enabled
    )
    {
        cursor_restore_background();


        cursor_enabled =
            false;


        return;
    }


    if (
        cursor_enabled
    )
    {
        return;
    }


    cursor_enabled =
        true;


    if (
        update_depth == 0
    )
    {
        cursor_capture_background();

        cursor_draw();
    }
}


/*
 * ============================================================
 * CURSOR MOVE
 * ============================================================
 */

void rootdisplay_cursor_move(
    i32 x,
    i32 y
)
{
    if (
        !display_available
    )
    {
        return;
    }


    if (
        x < 0
    )
    {
        x = 0;
    }


    if (
        y < 0
    )
    {
        y = 0;
    }


    if (
        x >=
        (i32)display_width
    )
    {
        x =
            (i32)display_width
            -
            1;
    }


    if (
        y >=
        (i32)display_height
    )
    {
        y =
            (i32)display_height
            -
            1;
    }


    if (
        x == cursor_x
        &&
        y == cursor_y
    )
    {
        return;
    }


    if (
        cursor_drawn
    )
    {
        cursor_restore_background();
    }


    cursor_x =
        x;


    cursor_y =
        y;


    if (
        cursor_enabled
        &&
        update_depth == 0
    )
    {
        cursor_capture_background();

        cursor_draw();
    }
}


/*
 * ============================================================
 * CURSOR VISIBLE
 * ============================================================
 */

bool rootdisplay_cursor_visible(void)
{
    return
        cursor_enabled
        &&
        cursor_drawn;
}