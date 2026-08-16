#include "terminal.h"
#include "io.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 25

#define VGA_MEMORY ((volatile u16*)0xB8000)

static u32 terminal_row = 0;
static u32 terminal_col = 0;

static u8 terminal_color = 0x0F;


/*
 * ===============================
 * CURSOR REAL DE VGA
 * ===============================
 */

static void terminal_update_hardware_cursor(void)
{
    u16 position =
        (u16)(
            terminal_row * VGA_WIDTH
            +
            terminal_col
        );

    /*
     * Parte baja de la posición.
     */
    outb(0x3D4, 0x0F);

    outb(
        0x3D5,
        (u8)(position & 0xFF)
    );

    /*
     * Parte alta.
     */
    outb(0x3D4, 0x0E);

    outb(
        0x3D5,
        (u8)((position >> 8) & 0xFF)
    );
}


static void terminal_enable_cursor(void)
{
    /*
     * Forma del cursor.
     */
    outb(0x3D4, 0x0A);

    u8 start =
        inb(0x3D5);

    outb(
        0x3D5,
        (start & 0xC0) | 13
    );


    outb(0x3D4, 0x0B);

    u8 end =
        inb(0x3D5);

    outb(
        0x3D5,
        (end & 0xE0) | 15
    );
}


/*
 * ===============================
 * VGA
 * ===============================
 */

static void terminal_put_at(
    char c,
    u32 x,
    u32 y
)
{
    VGA_MEMORY[
        y * VGA_WIDTH + x
    ] =
    (u16)(u8)c
    |
    ((u16)terminal_color << 8);
}


static void terminal_scroll(void)
{
    if (
        terminal_row
        <
        VGA_HEIGHT
    )
    {
        return;
    }


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
        terminal_put_at(
            ' ',
            x,
            VGA_HEIGHT - 1
        );
    }


    terminal_row =
        VGA_HEIGHT - 1;
}


void terminal_clear(void)
{
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
            terminal_put_at(
                ' ',
                x,
                y
            );
        }
    }


    terminal_row = 0;
    terminal_col = 0;


    terminal_enable_cursor();

    terminal_update_hardware_cursor();
}


void terminal_putchar(char c)
{
    if (c == '\n')
    {
        terminal_col = 0;

        terminal_row++;

        terminal_scroll();

        terminal_update_hardware_cursor();

        return;
    }


    if (c == '\b')
    {
        if (terminal_col > 0)
        {
            terminal_col--;

            terminal_put_at(
                ' ',
                terminal_col,
                terminal_row
            );
        }


        terminal_update_hardware_cursor();

        return;
    }


    terminal_put_at(
        c,
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


    terminal_update_hardware_cursor();
}


void terminal_print(
    const char* text
)
{
    while (*text)
    {
        terminal_putchar(
            *text
        );

        text++;
    }
}


/*
 * Cambiar manualmente la posición
 * del cursor.
 */
void terminal_set_cursor(
    u32 x,
    u32 y
)
{
    if (
        x >= VGA_WIDTH
        ||
        y >= VGA_HEIGHT
    )
    {
        return;
    }


    terminal_col = x;

    terminal_row = y;


    terminal_update_hardware_cursor();
}


u32 terminal_get_row(void)
{
    return terminal_row;
}


u32 terminal_get_col(void)
{
    return terminal_col;
}
