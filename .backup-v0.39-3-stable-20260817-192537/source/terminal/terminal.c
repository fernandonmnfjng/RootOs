#include "terminal.h"

#include "rootdisplay.h"
#include "rootfont.h"
#include "rootclipboard.h"

#include "unicode.h"
#include "string.h"
#include "memory.h"
#include "io.h"

/* ============================================================
 * TERMINAL CONFIGURATION
 * ============================================================ */

#define TERMINAL_CELL_WIDTH  8u
#define TERMINAL_CELL_HEIGHT 16u

#define TERMINAL_MAX_COLS 256u
#define TERMINAL_MAX_ROWS 128u
#define TERMINAL_SCROLLBACK_LINES 256u

#define TERMINAL_CONTINUATION 0xFFFFFFFFu

#define VGA_WIDTH 80u
#define VGA_HEIGHT 25u
#define VGA_MEMORY ((volatile u16*)0xB8000)

/* ============================================================
 * STATE
 * ============================================================ */

static bool terminal_graphics = false;

static u32 terminal_row = 0;
static u32 terminal_col = 0;
static u32 terminal_cols = VGA_WIDTH;
static u32 terminal_rows = VGA_HEIGHT;

static u32 terminal_foreground = 0;
static u32 terminal_background = 0;

static RootCodepoint terminal_cells[
    TERMINAL_MAX_ROWS
][
    TERMINAL_MAX_COLS
];

/* Fixed memory ring: at most 256 historical lines. */
static RootCodepoint scrollback[
    TERMINAL_SCROLLBACK_LINES
][
    TERMINAL_MAX_COLS
];

static u32 scrollback_head = 0;  /* next write */
static u32 scrollback_count = 0;
static u32 scrollback_view_offset = 0;

/* One render transaction can contain many characters. */
static u32 terminal_update_depth = 0;

/* ============================================================
 * TERMINAL SELECTION
 * ============================================================ */

static bool selection_tracking = false;
static bool selection_active_state = false;
static bool selection_drawn = false;

static u32 selection_anchor_col = 0;
static u32 selection_anchor_row = 0;
static u32 selection_focus_col = 0;
static u32 selection_focus_row = 0;

/* Avoid a 16 KiB clipboard buffer on the kernel stack. */
static RootCodepoint selection_copy_buffer[
    ROOT_CLIPBOARD_MAX_CODEPOINTS
];

/* ============================================================
 * VGA FALLBACK
 * ============================================================ */

static void vga_update_cursor(void)
{
    u16 position = (u16)(terminal_row * VGA_WIDTH + terminal_col);

    outb(0x3D4, 0x0F);
    outb(0x3D5, (u8)(position & 0xFFu));

    outb(0x3D4, 0x0E);
    outb(0x3D5, (u8)((position >> 8) & 0xFFu));
}

static void vga_enable_cursor(void)
{
    outb(0x3D4, 0x0A);
    u8 start = inb(0x3D5);
    outb(0x3D5, (start & 0xC0u) | 13u);

    outb(0x3D4, 0x0B);
    u8 end = inb(0x3D5);
    outb(0x3D5, (end & 0xE0u) | 15u);
}

static void vga_put_at(char c, u32 x, u32 y)
{
    VGA_MEMORY[y * VGA_WIDTH + x] =
        (u16)(u8)c | ((u16)0x0Fu << 8);
}

/* ============================================================
 * GRAPHICS RENDERING
 * ============================================================ */

static void graphics_clear_cell(u32 column, u32 row)
{
    rootdisplay_fill_rect(
        column * TERMINAL_CELL_WIDTH,
        row * TERMINAL_CELL_HEIGHT,
        TERMINAL_CELL_WIDTH,
        TERMINAL_CELL_HEIGHT,
        terminal_background
    );
}

static bool graphics_get_glyph(
    RootCodepoint codepoint,
    RootGlyph* glyph
)
{
    if (rootfont_get_glyph(codepoint, glyph))
        return true;

    return rootfont_get_glyph(ROOT_UNICODE_REPLACEMENT, glyph);
}

static void graphics_draw_glyph_pixels(
    RootCodepoint codepoint,
    u32 column,
    u32 row
)
{
    RootGlyph glyph;

    if (!graphics_get_glyph(codepoint, &glyph))
        return;

    u32 pixel_width = glyph.width;

    for (u32 y = 0; y < 16; y++)
    {
        for (u32 x = 0; x < pixel_width; x++)
        {
            u32 byte_index = y * 2u + x / 8u;
            u8 bit = (u8)(7u - (x % 8u));

            if ((glyph.bitmap[byte_index] & (1u << bit)) == 0)
                continue;

            rootdisplay_put_pixel(
                column * TERMINAL_CELL_WIDTH + x,
                row * TERMINAL_CELL_HEIGHT + y,
                terminal_foreground
            );
        }
    }
}

static void graphics_draw_glyph(
    RootCodepoint codepoint,
    u32 column,
    u32 row
)
{
    RootGlyph glyph;

    if (!graphics_get_glyph(codepoint, &glyph))
    {
        graphics_clear_cell(column, row);
        return;
    }

    rootdisplay_fill_rect(
        column * TERMINAL_CELL_WIDTH,
        row * TERMINAL_CELL_HEIGHT,
        glyph.width,
        TERMINAL_CELL_HEIGHT,
        terminal_background
    );

    graphics_draw_glyph_pixels(codepoint, column, row);
}

static void graphics_render_live_cell(u32 column, u32 row)
{
    if (column >= terminal_cols || row >= terminal_rows)
        return;

    RootCodepoint codepoint = terminal_cells[row][column];

    if (codepoint == TERMINAL_CONTINUATION)
    {
        if (column > 0)
        {
            RootCodepoint previous = terminal_cells[row][column - 1];
            if (
                previous != TERMINAL_CONTINUATION &&
                previous != 0 &&
                previous != ' '
            )
            {
                graphics_draw_glyph(previous, column - 1, row);
                return;
            }
        }

        graphics_clear_cell(column, row);
        return;
    }

    if (codepoint == 0 || codepoint == ' ')
    {
        graphics_clear_cell(column, row);
        return;
    }

    graphics_draw_glyph(codepoint, column, row);
}

/* ============================================================
 * SCROLLBACK / VISIBLE ROW MAPPING
 * ============================================================ */

static u32 scrollback_oldest_index(void)
{
    return
        (scrollback_head + TERMINAL_SCROLLBACK_LINES - scrollback_count) %
        TERMINAL_SCROLLBACK_LINES;
}

static const RootCodepoint* visible_row_data(u32 screen_row)
{
    if (screen_row >= terminal_rows)
        return NULL;

    u32 total_lines = scrollback_count + terminal_rows;
    u32 offset = scrollback_view_offset;

    if (offset > scrollback_count)
        offset = scrollback_count;

    u32 end = total_lines - offset;
    u32 start = end - terminal_rows;
    u32 logical = start + screen_row;

    if (logical < scrollback_count)
    {
        u32 index =
            (scrollback_oldest_index() + logical) %
            TERMINAL_SCROLLBACK_LINES;

        return scrollback[index];
    }

    return terminal_cells[logical - scrollback_count];
}

static void graphics_render_row_data(
    const RootCodepoint* row_data,
    u32 screen_row
)
{
    rootdisplay_fill_rect(
        0,
        screen_row * TERMINAL_CELL_HEIGHT,
        terminal_cols * TERMINAL_CELL_WIDTH,
        TERMINAL_CELL_HEIGHT,
        terminal_background
    );

    if (row_data == NULL)
        return;

    for (u32 column = 0; column < terminal_cols; column++)
    {
        RootCodepoint codepoint = row_data[column];

        if (
            codepoint == 0 ||
            codepoint == ' ' ||
            codepoint == TERMINAL_CONTINUATION
        )
        {
            continue;
        }

        graphics_draw_glyph_pixels(codepoint, column, screen_row);
    }
}

static void graphics_redraw_view(void)
{
    if (!terminal_graphics)
        return;

    rootdisplay_begin_update();

    rootdisplay_clear(terminal_background);

    for (u32 row = 0; row < terminal_rows; row++)
        graphics_render_row_data(visible_row_data(row), row);

    rootdisplay_end_update();
}

static void scrollback_push_top_row(void)
{
    for (u32 column = 0; column < terminal_cols; column++)
    {
        scrollback[scrollback_head][column] =
            terminal_cells[0][column];
    }

    scrollback_head =
        (scrollback_head + 1) % TERMINAL_SCROLLBACK_LINES;

    if (scrollback_count < TERMINAL_SCROLLBACK_LINES)
        scrollback_count++;
}

/* ============================================================
 * TEXT CURSOR
 * ============================================================ */

static void graphics_erase_cursor(void)
{
    if (!terminal_graphics || scrollback_view_offset != 0)
        return;

    graphics_render_live_cell(terminal_col, terminal_row);
}

static void graphics_draw_cursor(void)
{
    if (!terminal_graphics || scrollback_view_offset != 0)
        return;

    rootdisplay_fill_rect(
        terminal_col * TERMINAL_CELL_WIDTH,
        terminal_row * TERMINAL_CELL_HEIGHT + 14u,
        TERMINAL_CELL_WIDTH,
        2u,
        terminal_foreground
    );
}

/* ============================================================
 * SELECTION VISUAL
 * ============================================================ */

static u32 cell_linear(u32 column, u32 row)
{
    return row * terminal_cols + column;
}

static void selection_bounds(
    u32* start_col,
    u32* start_row,
    u32* end_col,
    u32* end_row
)
{
    u32 anchor = cell_linear(selection_anchor_col, selection_anchor_row);
    u32 focus = cell_linear(selection_focus_col, selection_focus_row);

    if (anchor <= focus)
    {
        *start_col = selection_anchor_col;
        *start_row = selection_anchor_row;
        *end_col = selection_focus_col;
        *end_row = selection_focus_row;
    }
    else
    {
        *start_col = selection_focus_col;
        *start_row = selection_focus_row;
        *end_col = selection_anchor_col;
        *end_row = selection_anchor_row;
    }
}

static void selection_toggle_visual(void)
{
    if (
        !terminal_graphics ||
        !selection_active_state
    )
    {
        return;
    }

    u32 start_col;
    u32 start_row;
    u32 end_col;
    u32 end_row;

    selection_bounds(
        &start_col,
        &start_row,
        &end_col,
        &end_row
    );

    rootdisplay_begin_update();

    for (u32 row = start_row; row <= end_row; row++)
    {
        u32 from = row == start_row ? start_col : 0;
        u32 to = row == end_row ? end_col : terminal_cols - 1;

        if (to < from)
            continue;

        rootdisplay_invert_rect(
            from * TERMINAL_CELL_WIDTH,
            row * TERMINAL_CELL_HEIGHT,
            (to - from + 1u) * TERMINAL_CELL_WIDTH,
            TERMINAL_CELL_HEIGHT
        );
    }

    rootdisplay_end_update();
}

static void selection_hide(void)
{
    if (!selection_drawn)
        return;

    selection_toggle_visual();
    selection_drawn = false;
}

static void selection_show(void)
{
    if (!selection_active_state || selection_drawn)
        return;

    selection_toggle_visual();
    selection_drawn = true;
}

/* ============================================================
 * UPDATE TRANSACTIONS
 * ============================================================ */

static void terminal_return_live(void)
{
    if (scrollback_view_offset == 0)
        return;

    terminal_selection_clear();
    scrollback_view_offset = 0;
    graphics_redraw_view();
}

static void terminal_begin_update(bool clear_selection)
{
    if (!terminal_graphics)
    {
        terminal_update_depth++;
        return;
    }

    if (terminal_update_depth == 0)
    {
        terminal_return_live();

        if (clear_selection)
            terminal_selection_clear();

        rootdisplay_begin_update();
        graphics_erase_cursor();
    }

    terminal_update_depth++;
}

static void terminal_end_update(void)
{
    if (terminal_update_depth == 0)
        return;

    terminal_update_depth--;

    if (terminal_update_depth != 0)
        return;

    if (terminal_graphics)
    {
        graphics_draw_cursor();
        rootdisplay_end_update();
    }
    else
    {
        vga_update_cursor();
    }
}

void terminal_begin_batch(void)
{
    terminal_begin_update(false);
}

void terminal_end_batch(void)
{
    terminal_end_update();
}

/* ============================================================
 * SCROLL
 * ============================================================ */

static void terminal_scroll(void)
{
    if (terminal_row < terminal_rows)
        return;

    if (terminal_graphics)
    {
        scrollback_push_top_row();

        for (u32 row = 1; row < terminal_rows; row++)
        {
            for (u32 column = 0; column < terminal_cols; column++)
            {
                terminal_cells[row - 1][column] =
                    terminal_cells[row][column];
            }
        }

        for (u32 column = 0; column < terminal_cols; column++)
            terminal_cells[terminal_rows - 1][column] = ' ';

        terminal_row = terminal_rows - 1;

        /* Pixel scroll instead of rasterizing the full terminal. */
        rootdisplay_scroll_up(
            TERMINAL_CELL_HEIGHT,
            terminal_background
        );

        return;
    }

    for (u32 y = 1; y < VGA_HEIGHT; y++)
    {
        for (u32 x = 0; x < VGA_WIDTH; x++)
        {
            VGA_MEMORY[(y - 1) * VGA_WIDTH + x] =
                VGA_MEMORY[y * VGA_WIDTH + x];
        }
    }

    for (u32 x = 0; x < VGA_WIDTH; x++)
        vga_put_at(' ', x, VGA_HEIGHT - 1);

    terminal_row = VGA_HEIGHT - 1;
}

/* ============================================================
 * INITIALIZATION / CLEAR
 * ============================================================ */

void terminal_init(void)
{
    terminal_row = 0;
    terminal_col = 0;

    scrollback_head = 0;
    scrollback_count = 0;
    scrollback_view_offset = 0;

    selection_tracking = false;
    selection_active_state = false;
    selection_drawn = false;

    terminal_update_depth = 0;

    terminal_graphics =
        rootdisplay_ready() && rootfont_ready();

    if (terminal_graphics)
    {
        terminal_cols = rootdisplay_width() / TERMINAL_CELL_WIDTH;
        terminal_rows = rootdisplay_height() / TERMINAL_CELL_HEIGHT;

        if (terminal_cols > TERMINAL_MAX_COLS)
            terminal_cols = TERMINAL_MAX_COLS;

        if (terminal_rows > TERMINAL_MAX_ROWS)
            terminal_rows = TERMINAL_MAX_ROWS;

        /* Actual white, not grey/yellow. */
        terminal_foreground = rootdisplay_rgb(255, 255, 255);
        terminal_background = rootdisplay_rgb(0, 0, 0);
    }
    else
    {
        terminal_cols = VGA_WIDTH;
        terminal_rows = VGA_HEIGHT;
        vga_enable_cursor();
    }

    for (u32 row = 0; row < TERMINAL_MAX_ROWS; row++)
    {
        for (u32 column = 0; column < TERMINAL_MAX_COLS; column++)
            terminal_cells[row][column] = ' ';
    }
}

void terminal_clear(void)
{
    terminal_begin_update(true);

    terminal_row = 0;
    terminal_col = 0;
    scrollback_view_offset = 0;

    if (terminal_graphics)
    {
        for (u32 row = 0; row < terminal_rows; row++)
        {
            for (u32 column = 0; column < terminal_cols; column++)
                terminal_cells[row][column] = ' ';
        }

        rootdisplay_clear(terminal_background);
    }
    else
    {
        for (u32 y = 0; y < VGA_HEIGHT; y++)
        {
            for (u32 x = 0; x < VGA_WIDTH; x++)
                vga_put_at(' ', x, y);
        }
    }

    terminal_end_update();
}

void terminal_clear_row(u32 row)
{
    if (row >= terminal_rows)
        return;

    terminal_begin_update(false);

    for (u32 column = 0; column < terminal_cols; column++)
        terminal_cells[row][column] = ' ';

    if (terminal_graphics)
    {
        rootdisplay_fill_rect(
            0,
            row * TERMINAL_CELL_HEIGHT,
            terminal_cols * TERMINAL_CELL_WIDTH,
            TERMINAL_CELL_HEIGHT,
            terminal_background
        );
    }
    else
    {
        for (u32 column = 0; column < VGA_WIDTH; column++)
            vga_put_at(' ', column, row);
    }

    terminal_end_update();
}

/* ============================================================
 * OUTPUT
 * ============================================================ */

static void terminal_putcodepoint_internal(RootCodepoint codepoint)
{
    if (codepoint == '\n')
    {
        terminal_col = 0;
        terminal_row++;
        terminal_scroll();
        return;
    }

    if (codepoint == '\r')
    {
        terminal_col = 0;
        return;
    }

    if (codepoint == '\t')
    {
        u32 spaces = 4u - (terminal_col % 4u);
        for (u32 i = 0; i < spaces; i++)
            terminal_putcodepoint_internal(' ');
        return;
    }

    if (codepoint == '\b')
    {
        if (terminal_col == 0)
            return;

        terminal_col--;
        terminal_cells[terminal_row][terminal_col] = ' ';

        if (terminal_graphics)
            graphics_render_live_cell(terminal_col, terminal_row);
        else
            vga_put_at(' ', terminal_col, terminal_row);

        return;
    }

    if (terminal_graphics)
    {
        RootGlyph glyph;
        if (!graphics_get_glyph(codepoint, &glyph))
            return;

        u32 cells_needed = glyph.width > 8 ? 2u : 1u;

        if (terminal_col + cells_needed > terminal_cols)
        {
            terminal_col = 0;
            terminal_row++;
            terminal_scroll();
        }

        /* If overwriting a continuation, clear the previous wide glyph. */
        if (
            terminal_cells[terminal_row][terminal_col] ==
            TERMINAL_CONTINUATION &&
            terminal_col > 0
        )
        {
            terminal_cells[terminal_row][terminal_col - 1] = ' ';
            graphics_render_live_cell(terminal_col - 1, terminal_row);
        }

        /* If replacing a wide glyph with narrow text, clear its continuation. */
        if (
            terminal_cells[terminal_row][terminal_col] !=
            TERMINAL_CONTINUATION &&
            terminal_col + 1 < terminal_cols &&
            terminal_cells[terminal_row][terminal_col + 1] ==
            TERMINAL_CONTINUATION
        )
        {
            terminal_cells[terminal_row][terminal_col + 1] = ' ';
            graphics_clear_cell(terminal_col + 1, terminal_row);
        }

        terminal_cells[terminal_row][terminal_col] = codepoint;

        if (cells_needed == 2 && terminal_col + 1 < terminal_cols)
            terminal_cells[terminal_row][terminal_col + 1] = TERMINAL_CONTINUATION;

        graphics_draw_glyph(codepoint, terminal_col, terminal_row);

        terminal_col += cells_needed;

        if (terminal_col >= terminal_cols)
        {
            terminal_col = 0;
            terminal_row++;
            terminal_scroll();
        }

        return;
    }

    char fallback = codepoint <= 0x7Fu ? (char)codepoint : '?';
    vga_put_at(fallback, terminal_col, terminal_row);

    terminal_col++;

    if (terminal_col >= VGA_WIDTH)
    {
        terminal_col = 0;
        terminal_row++;
        terminal_scroll();
    }
}

void terminal_putcodepoint(RootCodepoint codepoint)
{
    terminal_begin_update(true);
    terminal_putcodepoint_internal(codepoint);
    terminal_end_update();
}

void terminal_putchar(char c)
{
    terminal_putcodepoint((RootCodepoint)(u8)c);
}

void terminal_print(const char* text)
{
    if (text == NULL)
        return;

    usize length = root_strlen(text);
    usize offset = 0;

    /* One cursor/mouse hide+restore for the entire string. */
    terminal_begin_update(true);

    while (offset < length)
    {
        RootCodepoint codepoint;
        usize consumed = root_utf8_decode(
            text + offset,
            length - offset,
            &codepoint
        );

        if (consumed == 0)
        {
            codepoint = ROOT_UNICODE_REPLACEMENT;
            consumed = 1;
        }

        terminal_putcodepoint_internal(codepoint);
        offset += consumed;
    }

    terminal_end_update();
}

/* ============================================================
 * CURSOR / GEOMETRY
 * ============================================================ */

void terminal_set_cursor(u32 x, u32 y)
{
    if (x >= terminal_cols || y >= terminal_rows)
        return;

    terminal_begin_update(false);
    terminal_col = x;
    terminal_row = y;
    terminal_end_update();
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

u32 terminal_codepoint_cells(RootCodepoint codepoint)
{
    if (!terminal_graphics)
        return 1;

    RootGlyph glyph;
    if (!graphics_get_glyph(codepoint, &glyph))
        return 1;

    return glyph.width > 8 ? 2u : 1u;
}

bool terminal_pixel_to_cell(
    i32 pixel_x,
    i32 pixel_y,
    u32* column,
    u32* row
)
{
    if (
        !terminal_graphics ||
        column == NULL ||
        row == NULL ||
        pixel_x < 0 ||
        pixel_y < 0
    )
    {
        return false;
    }

    u32 c = (u32)pixel_x / TERMINAL_CELL_WIDTH;
    u32 r = (u32)pixel_y / TERMINAL_CELL_HEIGHT;

    if (c >= terminal_cols || r >= terminal_rows)
        return false;

    *column = c;
    *row = r;
    return true;
}

/* ============================================================
 * TERMINAL SELECTION
 * ============================================================ */

void terminal_selection_clear(void)
{
    selection_hide();
    selection_tracking = false;
    selection_active_state = false;
}

bool terminal_selection_begin(i32 pixel_x, i32 pixel_y)
{
    u32 column;
    u32 row;

    if (!terminal_pixel_to_cell(pixel_x, pixel_y, &column, &row))
        return false;

    terminal_selection_clear();

    selection_anchor_col = column;
    selection_anchor_row = row;
    selection_focus_col = column;
    selection_focus_row = row;
    selection_tracking = true;
    return true;
}

bool terminal_selection_drag(i32 pixel_x, i32 pixel_y)
{
    if (!selection_tracking)
        return false;

    u32 column;
    u32 row;

    if (!terminal_pixel_to_cell(pixel_x, pixel_y, &column, &row))
        return false;

    if (
        column == selection_focus_col &&
        row == selection_focus_row
    )
    {
        return true;
    }

    selection_hide();

    selection_focus_col = column;
    selection_focus_row = row;
    selection_active_state = true;

    selection_show();
    return true;
}

void terminal_selection_end(void)
{
    selection_tracking = false;
}

bool terminal_selection_active(void)
{
    return selection_active_state;
}

bool terminal_selection_copy(void)
{
    if (!selection_active_state)
        return false;

    u32 start_col;
    u32 start_row;
    u32 end_col;
    u32 end_row;

    selection_bounds(
        &start_col,
        &start_row,
        &end_col,
        &end_row
    );

    usize output = 0;

    for (u32 row = start_row; row <= end_row; row++)
    {
        const RootCodepoint* row_data = visible_row_data(row);
        if (row_data == NULL)
            continue;

        u32 from = row == start_row ? start_col : 0;
        u32 to = row == end_row ? end_col : terminal_cols - 1;

        /* Normal terminal copy trims unused trailing cells. */
        while (to > from)
        {
            RootCodepoint value = row_data[to];
            if (value != 0 && value != ' ')
                break;
            to--;
        }

        for (u32 column = from; column <= to; column++)
        {
            RootCodepoint value = row_data[column];

            if (value == TERMINAL_CONTINUATION)
                continue;

            if (value == 0)
                value = ' ';

            if (output + 1 >= ROOT_CLIPBOARD_MAX_CODEPOINTS)
                return rootclipboard_set(selection_copy_buffer, output);

            selection_copy_buffer[output++] = value;
        }

        if (row != end_row)
        {
            if (output + 1 >= ROOT_CLIPBOARD_MAX_CODEPOINTS)
                return rootclipboard_set(selection_copy_buffer, output);

            selection_copy_buffer[output++] = '\n';
        }
    }

    return rootclipboard_set(selection_copy_buffer, output);
}

static bool word_separator(RootCodepoint codepoint)
{
    return
        codepoint == 0 ||
        codepoint == ' ' ||
        codepoint == '\t' ||
        codepoint == TERMINAL_CONTINUATION;
}

bool terminal_selection_select_word(i32 pixel_x, i32 pixel_y)
{
    u32 column;
    u32 row;

    if (!terminal_pixel_to_cell(pixel_x, pixel_y, &column, &row))
        return false;

    const RootCodepoint* row_data = visible_row_data(row);
    if (row_data == NULL || word_separator(row_data[column]))
        return false;

    terminal_selection_clear();

    u32 start = column;
    u32 end = column;

    while (start > 0 && !word_separator(row_data[start - 1]))
        start--;

    while (
        end + 1 < terminal_cols &&
        !word_separator(row_data[end + 1])
    )
    {
        end++;
    }

    selection_anchor_col = start;
    selection_anchor_row = row;
    selection_focus_col = end;
    selection_focus_row = row;
    selection_active_state = true;
    selection_tracking = false;

    selection_show();
    return true;
}

/* ============================================================
 * SCROLLBACK VIEW
 * ============================================================ */

void terminal_scrollback_up(u32 lines)
{
    if (!terminal_graphics || scrollback_count == 0 || lines == 0)
        return;

    terminal_selection_clear();

    if (lines > scrollback_count - scrollback_view_offset)
        scrollback_view_offset = scrollback_count;
    else
        scrollback_view_offset += lines;

    graphics_redraw_view();
}

void terminal_scrollback_down(u32 lines)
{
    if (!terminal_graphics || lines == 0)
        return;

    terminal_selection_clear();

    if (lines >= scrollback_view_offset)
        scrollback_view_offset = 0;
    else
        scrollback_view_offset -= lines;

    graphics_redraw_view();

    if (scrollback_view_offset == 0)
    {
        rootdisplay_begin_update();
        graphics_draw_cursor();
        rootdisplay_end_update();
    }
}

void terminal_scrollback_bottom(void)
{
    if (scrollback_view_offset == 0)
        return;

    terminal_selection_clear();
    scrollback_view_offset = 0;
    graphics_redraw_view();

    if (terminal_graphics)
    {
        rootdisplay_begin_update();
        graphics_draw_cursor();
        rootdisplay_end_update();
    }
}

u32 terminal_scrollback_available(void)
{
    return scrollback_count;
}
