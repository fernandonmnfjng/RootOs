#include "terminal.h"

#include "rootdisplay.h"
#include "rootfont.h"

#include "unicode.h"
#include "string.h"
#include "memory.h"

#include "io.h"


/*
 * ============================================================
 * TERMINAL
 * ============================================================
 */

#define TERMINAL_CELL_WIDTH  8
#define TERMINAL_CELL_HEIGHT 16

#define TERMINAL_MAX_COLS 256
#define TERMINAL_MAX_ROWS 128


#define TERMINAL_CONTINUATION \
    0xFFFFFFFFu


/*
 * Fallback VGA.
 */
#define VGA_WIDTH 80
#define VGA_HEIGHT 25

#define VGA_MEMORY \
    ((volatile u16*)0xB8000)


static bool terminal_graphics =
    false;


static u32 terminal_row = 0;
static u32 terminal_col = 0;

static u32 terminal_cols =
    VGA_WIDTH;

static u32 terminal_rows =
    VGA_HEIGHT;


static u32 terminal_foreground = 0;

static u32 terminal_background = 0;


static RootCodepoint terminal_cells[
    TERMINAL_MAX_ROWS
][
    TERMINAL_MAX_COLS
];


/*
 * ============================================================
 * VGA FALLBACK
 * ============================================================
 */

static void vga_update_cursor(void)
{
    u16 position =
        (
            u16
        )(
            terminal_row
            *
            VGA_WIDTH
            +
            terminal_col
        );


    outb(
        0x3D4,
        0x0F
    );

    outb(
        0x3D5,
        (
            u8
        )(
            position
            &
            0xFF
        )
    );


    outb(
        0x3D4,
        0x0E
    );

    outb(
        0x3D5,
        (
            u8
        )(
            (
                position
                >>
                8
            )
            &
            0xFF
        )
    );
}


static void vga_enable_cursor(void)
{
    outb(
        0x3D4,
        0x0A
    );


    u8 start =
        inb(
            0x3D5
        );


    outb(
        0x3D5,
        (
            start
            &
            0xC0
        )
        |
        13
    );


    outb(
        0x3D4,
        0x0B
    );


    u8 end =
        inb(
            0x3D5
        );


    outb(
        0x3D5,
        (
            end
            &
            0xE0
        )
        |
        15
    );
}


static void vga_put_at(
    char c,
    u32 x,
    u32 y
)
{
    VGA_MEMORY[
        y * VGA_WIDTH + x
    ] =
        (
            u16
        )(
            u8
        )c
        |
        (
            (
                u16
            )
            0x0F
            <<
            8
        );
}


/*
 * ============================================================
 * GRAPHICAL CELL RENDERING
 * ============================================================
 */

static void graphics_clear_cell(
    u32 column,
    u32 row
)
{
    rootdisplay_fill_rect(
        column
            *
            TERMINAL_CELL_WIDTH,

        row
            *
            TERMINAL_CELL_HEIGHT,

        TERMINAL_CELL_WIDTH,

        TERMINAL_CELL_HEIGHT,

        terminal_background
    );
}


static void graphics_draw_glyph(
    RootCodepoint codepoint,
    u32 column,
    u32 row
)
{
    RootGlyph glyph;


    if (
        !rootfont_get_glyph(
            codepoint,
            &glyph
        )
    )
    {
        graphics_clear_cell(
            column,
            row
        );

        return;
    }


    /*
     * Un glifo ancho ocupa 2 celdas.
     */
    u32 pixel_width =
        glyph.width;


    rootdisplay_fill_rect(
        column
            *
            TERMINAL_CELL_WIDTH,

        row
            *
            TERMINAL_CELL_HEIGHT,

        pixel_width,

        TERMINAL_CELL_HEIGHT,

        terminal_background
    );


    for (
        u32 y = 0;
        y < 16;
        y++
    )
    {
        for (
            u32 x = 0;
            x < pixel_width;
            x++
        )
        {
            u32 byte_index =
                y * 2
                +
                x / 8;


            u8 bit =
                (
                    u8
                )(
                    7
                    -
                    (
                        x % 8
                    )
                );


            bool set =
                (
                    glyph.bitmap[
                        byte_index
                    ]
                    &
                    (
                        1u
                        <<
                        bit
                    )
                )
                !=
                0;


            if (set)
            {
                rootdisplay_put_pixel(
                    column
                        *
                        TERMINAL_CELL_WIDTH
                        +
                        x,

                    row
                        *
                        TERMINAL_CELL_HEIGHT
                        +
                        y,

                    terminal_foreground
                );
            }
        }
    }
}


static void graphics_render_cell(
    u32 column,
    u32 row
)
{
    RootCodepoint codepoint =
        terminal_cells[
            row
        ][
            column
        ];


    /*
     * Segunda mitad de un glifo ancho.
     */
    if (
        codepoint
        ==
        TERMINAL_CONTINUATION
    )
    {
        if (
            column > 0
        )
        {
            RootCodepoint previous =
                terminal_cells[
                    row
                ][
                    column - 1
                ];


            if (
                previous
                !=
                TERMINAL_CONTINUATION
            )
            {
                graphics_draw_glyph(
                    previous,
                    column - 1,
                    row
                );

                return;
            }
        }


        graphics_clear_cell(
            column,
            row
        );

        return;
    }


    if (
        codepoint == 0
        ||
        codepoint == ' '
    )
    {
        graphics_clear_cell(
            column,
            row
        );

        return;
    }


    graphics_draw_glyph(
        codepoint,
        column,
        row
    );
}


static void graphics_redraw_all(void)
{
    rootdisplay_clear(
        terminal_background
    );


    for (
        u32 row = 0;
        row < terminal_rows;
        row++
    )
    {
        for (
            u32 column = 0;
            column < terminal_cols;
            column++
        )
        {
            if (
                terminal_cells[
                    row
                ][
                    column
                ]
                ==
                TERMINAL_CONTINUATION
            )
            {
                continue;
            }


            graphics_render_cell(
                column,
                row
            );
        }
    }
}


/*
 * ============================================================
 * SOFTWARE CURSOR
 * ============================================================
 */

static void graphics_erase_cursor(void)
{
    graphics_render_cell(
        terminal_col,
        terminal_row
    );
}


static void graphics_draw_cursor(void)
{
    rootdisplay_fill_rect(
        terminal_col
            *
            TERMINAL_CELL_WIDTH,

        terminal_row
            *
            TERMINAL_CELL_HEIGHT
            +
            14,

        TERMINAL_CELL_WIDTH,

        2,

        terminal_foreground
    );
}


/*
 * ============================================================
 * SCROLL
 * ============================================================
 */

static void terminal_scroll(void)
{
    if (
        terminal_row
        <
        terminal_rows
    )
    {
        return;
    }


    /*
     * Graphical terminal.
     */
    if (
        terminal_graphics
    )
    {
        for (
            u32 row = 1;
            row < terminal_rows;
            row++
        )
        {
            for (
                u32 column = 0;
                column < terminal_cols;
                column++
            )
            {
                terminal_cells[
                    row - 1
                ][
                    column
                ]
                =
                terminal_cells[
                    row
                ][
                    column
                ];
            }
        }


        for (
            u32 column = 0;
            column < terminal_cols;
            column++
        )
        {
            terminal_cells[
                terminal_rows - 1
            ][
                column
            ] =
                ' ';
        }


        terminal_row =
            terminal_rows - 1;


        graphics_redraw_all();

        return;
    }


    /*
     * VGA fallback.
     */
    for (
        u32 y = 1;
        y < VGA_HEIGHT;
        y++
    )
    {
        for (
            u32 x = 0;
            x < VGA_WIDTH;
            x++
        )
        {
            VGA_MEMORY[
                (y - 1)
                *
                VGA_WIDTH
                +
                x
            ]
            =
            VGA_MEMORY[
                y
                *
                VGA_WIDTH
                +
                x
            ];
        }
    }


    for (
        u32 x = 0;
        x < VGA_WIDTH;
        x++
    )
    {
        vga_put_at(
            ' ',
            x,
            VGA_HEIGHT - 1
        );
    }


    terminal_row =
        VGA_HEIGHT - 1;
}


/*
 * ============================================================
 * INITIALIZE
 * ============================================================
 */

void terminal_init(void)
{
    terminal_row = 0;
    terminal_col = 0;


    terminal_graphics =
        rootdisplay_ready()
        &&
        rootfont_ready();


    if (
        terminal_graphics
    )
    {
        terminal_cols =
            rootdisplay_width()
            /
            TERMINAL_CELL_WIDTH;


        terminal_rows =
            rootdisplay_height()
            /
            TERMINAL_CELL_HEIGHT;


        if (
            terminal_cols
            >
            TERMINAL_MAX_COLS
        )
        {
            terminal_cols =
                TERMINAL_MAX_COLS;
        }


        if (
            terminal_rows
            >
            TERMINAL_MAX_ROWS
        )
        {
            terminal_rows =
                TERMINAL_MAX_ROWS;
        }


        terminal_foreground =
            rootdisplay_rgb(
                235,
                235,
                235
            );


        terminal_background =
            rootdisplay_rgb(
                0,
                0,
                0
            );


        return;
    }


    terminal_cols =
        VGA_WIDTH;

    terminal_rows =
        VGA_HEIGHT;
}


/*
 * ============================================================
 * CLEAR
 * ============================================================
 */

void terminal_clear(void)
{
    terminal_row = 0;
    terminal_col = 0;


    if (
        terminal_graphics
    )
    {
        for (
            u32 row = 0;
            row < terminal_rows;
            row++
        )
        {
            for (
                u32 column = 0;
                column < terminal_cols;
                column++
            )
            {
                terminal_cells[
                    row
                ][
                    column
                ] =
                    ' ';
            }
        }


        rootdisplay_clear(
            terminal_background
        );


        graphics_draw_cursor();

        return;
    }


    for (
        u32 y = 0;
        y < VGA_HEIGHT;
        y++
    )
    {
        for (
            u32 x = 0;
            x < VGA_WIDTH;
            x++
        )
        {
            vga_put_at(
                ' ',
                x,
                y
            );
        }
    }


    vga_enable_cursor();

    vga_update_cursor();
}


/*
 * ============================================================
 * UNICODE CHARACTER
 * ============================================================
 */

void terminal_putcodepoint(
    RootCodepoint codepoint
)
{
    /*
     * NEWLINE
     */
    if (
        codepoint == '\n'
    )
    {
        if (
            terminal_graphics
        )
        {
            graphics_erase_cursor();
        }


        terminal_col = 0;

        terminal_row++;

        terminal_scroll();


        if (
            terminal_graphics
        )
        {
            graphics_draw_cursor();
        }

        else
        {
            vga_update_cursor();
        }


        return;
    }


    /*
     * CARRIAGE RETURN
     */
    if (
        codepoint == '\r'
    )
    {
        terminal_set_cursor(
            0,
            terminal_row
        );

        return;
    }


    /*
     * TAB = 4 spaces.
     */
    if (
        codepoint == '\t'
    )
    {
        u32 spaces =
            4
            -
            (
                terminal_col
                %
                4
            );


        for (
            u32 i = 0;
            i < spaces;
            i++
        )
        {
            terminal_putcodepoint(
                ' '
            );
        }


        return;
    }


    /*
     * BACKSPACE
     */
    if (
        codepoint == '\b'
    )
    {
        if (
            terminal_col == 0
        )
        {
            return;
        }


        if (
            terminal_graphics
        )
        {
            graphics_erase_cursor();
        }


        terminal_col--;


        if (
            terminal_graphics
        )
        {
            terminal_cells[
                terminal_row
            ][
                terminal_col
            ] =
                ' ';


            graphics_render_cell(
                terminal_col,
                terminal_row
            );


            graphics_draw_cursor();
        }

        else
        {
            vga_put_at(
                ' ',
                terminal_col,
                terminal_row
            );

            vga_update_cursor();
        }


        return;
    }


    /*
     * ========================================================
     * GRAPHICS
     * ========================================================
     */

    if (
        terminal_graphics
    )
    {
        RootGlyph glyph;


        if (
            !rootfont_get_glyph(
                codepoint,
                &glyph
            )
        )
        {
            codepoint =
                ROOT_UNICODE_REPLACEMENT;


            if (
                !rootfont_get_glyph(
                    codepoint,
                    &glyph
                )
            )
            {
                return;
            }
        }


        u32 cells_needed =
            (
                glyph.width > 8
            )
            ?
            2
            :
            1;


        /*
         * No cabe al final.
         */
        if (
            terminal_col
            +
            cells_needed
            >
            terminal_cols
        )
        {
            terminal_col = 0;

            terminal_row++;

            terminal_scroll();
        }


        graphics_erase_cursor();


        terminal_cells[
            terminal_row
        ][
            terminal_col
        ] =
            codepoint;


        if (
            cells_needed == 2
            &&
            terminal_col + 1
                <
                terminal_cols
        )
        {
            terminal_cells[
                terminal_row
            ][
                terminal_col + 1
            ] =
                TERMINAL_CONTINUATION;
        }


        graphics_draw_glyph(
            codepoint,
            terminal_col,
            terminal_row
        );


        terminal_col +=
            cells_needed;


        if (
            terminal_col
            >=
            terminal_cols
        )
        {
            terminal_col = 0;

            terminal_row++;

            terminal_scroll();
        }


        graphics_draw_cursor();

        return;
    }


    /*
     * ========================================================
     * VGA FALLBACK
     * ========================================================
     *
     * VGA no puede representar Unicode real.
     */

    char fallback;


    if (
        codepoint <= 0x7F
    )
    {
        fallback =
            (char)codepoint;
    }

    else
    {
        fallback = '?';
    }


    vga_put_at(
        fallback,
        terminal_col,
        terminal_row
    );


    terminal_col++;


    if (
        terminal_col
        >=
        VGA_WIDTH
    )
    {
        terminal_col = 0;

        terminal_row++;

        terminal_scroll();
    }


    vga_update_cursor();
}


/*
 * ============================================================
 * ASCII COMPATIBILITY
 * ============================================================
 */

void terminal_putchar(
    char c
)
{
    terminal_putcodepoint(
        (
            RootCodepoint
        )(
            u8
        )c
    );
}


/*
 * ============================================================
 * UTF-8 STRING
 * ============================================================
 */

void terminal_print(
    const char* text
)
{
    if (
        text == NULL
    )
    {
        return;
    }


    usize length =
        root_strlen(
            text
        );


    usize offset = 0;


    while (
        offset < length
    )
    {
        RootCodepoint codepoint;


        usize consumed =
            root_utf8_decode(
                text + offset,
                length - offset,
                &codepoint
            );


        if (
            consumed == 0
        )
        {
            codepoint =
                ROOT_UNICODE_REPLACEMENT;

            consumed = 1;
        }


        terminal_putcodepoint(
            codepoint
        );


        offset +=
            consumed;
    }
}


/*
 * ============================================================
 * CURSOR
 * ============================================================
 */

void terminal_set_cursor(
    u32 x,
    u32 y
)
{
    if (
        x >= terminal_cols
        ||
        y >= terminal_rows
    )
    {
        return;
    }


    if (
        terminal_graphics
    )
    {
        graphics_erase_cursor();
    }


    terminal_col = x;
    terminal_row = y;


    if (
        terminal_graphics
    )
    {
        graphics_draw_cursor();
    }

    else
    {
        vga_update_cursor();
    }
}


u32 terminal_get_row(void)
{
    return terminal_row;
}


u32 terminal_get_col(void)
{
    return terminal_col;
}

u32 terminal_get_columns(void)
{
    return terminal_cols;
}


u32 terminal_get_rows(void)
{
    return terminal_rows;
}


u32 terminal_codepoint_cells(
    RootCodepoint codepoint
)
{
    if (
        !terminal_graphics
    )
    {
        return 1;
    }


    RootGlyph glyph;


    if (
        !rootfont_get_glyph(
            codepoint,
            &glyph
        )
    )
    {
        return 1;
    }


    return
        glyph.width > 8
        ?
        2
        :
        1;
}


bool terminal_pixel_to_cell(
    i32 pixel_x,
    i32 pixel_y,
    u32* column,
    u32* row
)
{
    if (
        !terminal_graphics
        ||
        column == NULL
        ||
        row == NULL
    )
    {
        return false;
    }


    if (
        pixel_x < 0
        ||
        pixel_y < 0
    )
    {
        return false;
    }


    u32 c =
        (
            u32
        )
        pixel_x
        /
        TERMINAL_CELL_WIDTH;


    u32 r =
        (
            u32
        )
        pixel_y
        /
        TERMINAL_CELL_HEIGHT;


    if (
        c >= terminal_cols
        ||
        r >= terminal_rows
    )
    {
        return false;
    }


    *column = c;
    *row = r;


    return true;
}