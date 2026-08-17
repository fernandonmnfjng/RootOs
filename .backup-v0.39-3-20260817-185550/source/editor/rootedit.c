#include "rootedit.h"

#include "terminal.h"
#include "rootinput.h"
#include "rootdisplay.h"

#include "unicode.h"

#include "string.h"

#include "path.h"


#define ROOTEDIT_MAX_CODEPOINTS \
    FS_MAX_FILE_SIZE


#define ROOTEDIT_BODY_START_ROW 3


/*
 * El terminal actual usa celdas 8x16.
 */

#define ROOTEDIT_CELL_WIDTH  8
#define ROOTEDIT_CELL_HEIGHT 16


/*
 * ============================================================
 * EDITOR BUFFER
 * ============================================================
 */

static RootCodepoint editor_buffer[
    ROOTEDIT_MAX_CODEPOINTS
];


static char editor_file_bytes[
    FS_MAX_FILE_SIZE + 1
];


static char editor_save_bytes[
    FS_MAX_FILE_SIZE + 1
];


static char editor_path[
    ROOT_PATH_MAX
];


static usize editor_length =
    0;


static usize editor_cursor =
    0;


static bool editor_dirty =
    false;


static bool editor_save_error =
    false;


static u32 editor_view_line =
    0;


static u32 editor_horizontal_offset =
    0;


/*
 * ============================================================
 * SELECTION
 * ============================================================
 */

static bool selection_active =
    false;


static usize selection_anchor =
    0;


static bool selection_drawn =
    false;


static bool mouse_selecting =
    false;


/*
 * ============================================================
 * INTERNAL CLIPBOARD
 * ============================================================
 */

static RootCodepoint editor_clipboard[
    ROOTEDIT_MAX_CODEPOINTS
];


static usize editor_clipboard_length =
    0;


/*
 * ============================================================
 * SCREEN SIZE
 * ============================================================
 */

static u32 editor_columns(void)
{
    u32 columns =
        terminal_get_columns();


    if (
        columns < 2
    )
    {
        return 1;
    }


    return
        columns - 1;
}


static u32 editor_rows(void)
{
    u32 rows =
        terminal_get_rows();


    if (
        rows < 6
    )
    {
        return 6;
    }


    return rows;
}


static u32 editor_body_rows(void)
{
    return
        editor_rows()
        -
        4;
}


/*
 * ============================================================
 * CLEAR ROW
 * ============================================================
 *
 * En framebuffer limpiamos directamente 16 líneas de píxeles.
 * Es mucho más rápido que imprimir 127 espacios.
 */

static void editor_clear_row(
    u32 row
)
{
    if (
        rootdisplay_ready()
    )
    {
        rootdisplay_fill_rect(
            0,
            row
            *
            ROOTEDIT_CELL_HEIGHT,

            rootdisplay_width(),
            ROOTEDIT_CELL_HEIGHT,

            rootdisplay_rgb(
                0,
                0,
                0
            )
        );


        terminal_set_cursor(
            0,
            row
        );


        return;
    }


    u32 columns =
        editor_columns();


    terminal_set_cursor(
        0,
        row
    );


    for (
        u32 i = 0;
        i < columns;
        i++
    )
    {
        terminal_putchar(
            ' '
        );
    }


    terminal_set_cursor(
        0,
        row
    );
}


/*
 * ============================================================
 * LINE HELPERS
 * ============================================================
 */

static usize editor_line_start_from_index(
    usize index
)
{
    if (
        index > editor_length
    )
    {
        index =
            editor_length;
    }


    while (
        index > 0
        &&
        editor_buffer[
            index - 1
        ]
        !=
        '\n'
    )
    {
        index--;
    }


    return index;
}


static usize editor_line_end_from_index(
    usize index
)
{
    if (
        index > editor_length
    )
    {
        index =
            editor_length;
    }


    while (
        index < editor_length
        &&
        editor_buffer[index]
        !=
        '\n'
    )
    {
        index++;
    }


    return index;
}


static u32 editor_line_number_from_index(
    usize index
)
{
    u32 line =
        0;


    if (
        index > editor_length
    )
    {
        index =
            editor_length;
    }


    for (
        usize i = 0;
        i < index;
        i++
    )
    {
        if (
            editor_buffer[i]
            ==
            '\n'
        )
        {
            line++;
        }
    }


    return line;
}


static usize editor_line_start_by_number(
    u32 line
)
{
    if (
        line == 0
    )
    {
        return 0;
    }


    u32 current =
        0;


    for (
        usize i = 0;
        i < editor_length;
        i++
    )
    {
        if (
            editor_buffer[i]
            ==
            '\n'
        )
        {
            current++;


            if (
                current == line
            )
            {
                return
                    i + 1;
            }
        }
    }


    return
        editor_length;
}


static u32 editor_visual_column(
    usize line_start,
    usize index
)
{
    u32 column =
        0;


    if (
        index > editor_length
    )
    {
        index =
            editor_length;
    }


    for (
        usize i = line_start;
        i < index;
        i++
    )
    {
        if (
            editor_buffer[i]
            ==
            '\n'
        )
        {
            break;
        }


        column +=
            terminal_codepoint_cells(
                editor_buffer[i]
            );
    }


    return column;
}


static u32 editor_codepoint_column(
    usize index
)
{
    usize start =
        editor_line_start_from_index(
            index
        );


    return
        (u32)(
            index
            -
            start
        );
}


/*
 * ============================================================
 * SELECTION BOUNDS
 * ============================================================
 */

static usize editor_selection_start(void)
{
    if (
        selection_anchor
        <
        editor_cursor
    )
    {
        return
            selection_anchor;
    }


    return
        editor_cursor;
}


static usize editor_selection_end(void)
{
    if (
        selection_anchor
        >
        editor_cursor
    )
    {
        return
            selection_anchor;
    }


    return
        editor_cursor;
}


static void editor_selection_clear(void)
{
    selection_active =
        false;


    selection_anchor =
        editor_cursor;


    selection_drawn =
        false;
}


/*
 * ============================================================
 * CHECK NEWLINE IN RANGE
 * ============================================================
 */

static bool editor_range_has_newline(
    usize start,
    usize end
)
{
    if (
        start > end
    )
    {
        usize temporary =
            start;


        start =
            end;


        end =
            temporary;
    }


    if (
        end > editor_length
    )
    {
        end =
            editor_length;
    }


    for (
        usize i = start;
        i < end;
        i++
    )
    {
        if (
            editor_buffer[i]
            ==
            '\n'
        )
        {
            return true;
        }
    }


    return false;
}


/*
 * ============================================================
 * SELECTION VISUAL
 * ============================================================
 */

static void editor_toggle_selection_visual(void)
{
    if (
        !selection_active
        ||
        !rootdisplay_ready()
    )
    {
        return;
    }


    usize start =
        editor_selection_start();


    usize end =
        editor_selection_end();


    if (
        start == end
    )
    {
        return;
    }


    u32 first_visible_line =
        editor_view_line;


    u32 last_visible_line =
        editor_view_line
        +
        editor_body_rows();


    u32 start_line =
        editor_line_number_from_index(
            start
        );


    u32 end_line =
        editor_line_number_from_index(
            end
        );


    if (
        end_line
        <
        first_visible_line
        ||
        start_line
        >=
        last_visible_line
    )
    {
        return;
    }


    rootdisplay_begin_update();


    u32 from_line =
        start_line;


    if (
        from_line
        <
        first_visible_line
    )
    {
        from_line =
            first_visible_line;
    }


    u32 to_line =
        end_line;


    if (
        to_line
        >=
        last_visible_line
    )
    {
        to_line =
            last_visible_line
            -
            1;
    }


    for (
        u32 line = from_line;
        line <= to_line;
        line++
    )
    {
        usize line_start =
            editor_line_start_by_number(
                line
            );


        usize line_end =
            editor_line_end_from_index(
                line_start
            );


        usize segment_start =
            start;


        usize segment_end =
            end;


        if (
            segment_start
            <
            line_start
        )
        {
            segment_start =
                line_start;
        }


        if (
            segment_end
            >
            line_end
        )
        {
            segment_end =
                line_end;
        }


        if (
            segment_start
            >=
            segment_end
        )
        {
            continue;
        }


        u32 visual_start =
            editor_visual_column(
                line_start,
                segment_start
            );


        u32 visual_end =
            editor_visual_column(
                line_start,
                segment_end
            );


        if (
            visual_end
            <=
            editor_horizontal_offset
        )
        {
            continue;
        }


        if (
            visual_start
            <
            editor_horizontal_offset
        )
        {
            visual_start =
                editor_horizontal_offset;
        }


        u32 screen_start =
            visual_start
            -
            editor_horizontal_offset;


        u32 screen_end =
            visual_end
            -
            editor_horizontal_offset;


        u32 columns =
            editor_columns();


        if (
            screen_start >= columns
        )
        {
            continue;
        }


        if (
            screen_end > columns
        )
        {
            screen_end =
                columns;
        }


        if (
            screen_end
            <=
            screen_start
        )
        {
            continue;
        }


        u32 screen_row =
            ROOTEDIT_BODY_START_ROW
            +
            (
                line
                -
                editor_view_line
            );


        rootdisplay_invert_rect(
            screen_start
            *
            ROOTEDIT_CELL_WIDTH,

            screen_row
            *
            ROOTEDIT_CELL_HEIGHT,

            (
                screen_end
                -
                screen_start
            )
            *
            ROOTEDIT_CELL_WIDTH,

            ROOTEDIT_CELL_HEIGHT
        );
    }


    rootdisplay_end_update();
}


static void editor_selection_hide(void)
{
    if (
        selection_drawn
    )
    {
        editor_toggle_selection_visual();


        selection_drawn =
            false;
    }
}


static void editor_selection_show(void)
{
    if (
        selection_active
        &&
        editor_selection_start()
        !=
        editor_selection_end()
    )
    {
        editor_toggle_selection_visual();


        selection_drawn =
            true;
    }
}


/*
 * ============================================================
 * VIEWPORT
 * ============================================================
 */

static bool editor_ensure_cursor_visible(void)
{
    bool changed =
        false;


    u32 cursor_line =
        editor_line_number_from_index(
            editor_cursor
        );


    u32 body_rows =
        editor_body_rows();


    if (
        cursor_line
        <
        editor_view_line
    )
    {
        editor_view_line =
            cursor_line;


        changed =
            true;
    }


    if (
        cursor_line
        >=
        editor_view_line
        +
        body_rows
    )
    {
        editor_view_line =
            cursor_line
            -
            body_rows
            +
            1;


        changed =
            true;
    }


    usize line_start =
        editor_line_start_from_index(
            editor_cursor
        );


    u32 visual =
        editor_visual_column(
            line_start,
            editor_cursor
        );


    u32 columns =
        editor_columns();


    if (
        visual
        <
        editor_horizontal_offset
    )
    {
        editor_horizontal_offset =
            visual;


        changed =
            true;
    }


    if (
        visual
        >=
        editor_horizontal_offset
        +
        columns
    )
    {
        editor_horizontal_offset =
            visual
            -
            columns
            +
            1;


        changed =
            true;
    }


    return changed;
}


/*
 * ============================================================
 * CURSOR
 * ============================================================
 */

static void editor_place_cursor(void)
{
    u32 line =
        editor_line_number_from_index(
            editor_cursor
        );


    usize line_start =
        editor_line_start_from_index(
            editor_cursor
        );


    u32 visual =
        editor_visual_column(
            line_start,
            editor_cursor
        );


    u32 x =
        0;


    if (
        visual
        >=
        editor_horizontal_offset
    )
    {
        x =
            visual
            -
            editor_horizontal_offset;
    }


    u32 y =
        ROOTEDIT_BODY_START_ROW
        +
        (
            line
            -
            editor_view_line
        );


    if (
        x >= editor_columns()
    )
    {
        x =
            editor_columns() - 1;
    }


    if (
        y >=
        editor_rows() - 1
    )
    {
        y =
            editor_rows() - 2;
    }


    terminal_set_cursor(
        x,
        y
    );
}


/*
 * ============================================================
 * DRAW ONE LOGICAL LINE
 * ============================================================
 */

static void editor_draw_line(
    u32 logical_line
)
{
    if (
        logical_line
        <
        editor_view_line
    )
    {
        return;
    }


    u32 screen_line =
        logical_line
        -
        editor_view_line;


    if (
        screen_line
        >=
        editor_body_rows()
    )
    {
        return;
    }


    u32 screen_row =
        ROOTEDIT_BODY_START_ROW
        +
        screen_line;


    editor_clear_row(
        screen_row
    );


    usize line_start =
        editor_line_start_by_number(
            logical_line
        );


    usize line_end =
        editor_line_end_from_index(
            line_start
        );


    u32 source_column =
        0;


    u32 output_column =
        0;


    terminal_set_cursor(
        0,
        screen_row
    );


    for (
        usize i = line_start;
        i < line_end;
        i++
    )
    {
        RootCodepoint codepoint =
            editor_buffer[i];


        u32 width =
            terminal_codepoint_cells(
                codepoint
            );


        if (
            source_column
            +
            width
            <=
            editor_horizontal_offset
        )
        {
            source_column +=
                width;


            continue;
        }


        if (
            output_column
            +
            width
            >
            editor_columns()
        )
        {
            break;
        }


        terminal_putcodepoint(
            codepoint
        );


        source_column +=
            width;


        output_column +=
            width;
    }
}


/*
 * ============================================================
 * DRAW DOCUMENT
 * ============================================================
 */

static void editor_draw_document(void)
{
    for (
        u32 i = 0;
        i < editor_body_rows();
        i++
    )
    {
        editor_draw_line(
            editor_view_line
            +
            i
        );
    }
}


/*
 * ============================================================
 * HEADER
 * ============================================================
 */

static void editor_draw_header(void)
{
    editor_clear_row(
        0
    );


    terminal_print(
        "RootEdit - "
    );


    terminal_print(
        editor_path
    );


    editor_clear_row(
        1
    );


    terminal_print(
        "Ctrl+S Save | Ctrl+Q Exit | Ctrl+C Copy | Ctrl+V Paste | Ctrl+K Cut"
    );


    editor_clear_row(
        2
    );


    for (
        u32 i = 0;
        i < editor_columns();
        i++
    )
    {
        terminal_putchar(
            '-'
        );
    }
}


/*
 * ============================================================
 * STATUS
 * ============================================================
 */

static void editor_draw_status(void)
{
    u32 row =
        editor_rows()
        -
        1;


    editor_clear_row(
        row
    );


    if (
        editor_save_error
    )
    {
        terminal_print(
            "[SAVE ERROR]"
        );
    }

    else if (
        editor_dirty
    )
    {
        terminal_print(
            "[MODIFIED]"
        );
    }

    else
    {
        terminal_print(
            "[SAVED]"
        );
    }


    if (
        selection_active
    )
    {
        terminal_print(
            " [SELECTION]"
        );
    }
}


/*
 * ============================================================
 * FULL REDRAW
 * ============================================================
 */

static void editor_redraw_full(void)
{
    /*
     * El nuevo dibujo sustituirá la selección antigua.
     */

    selection_drawn =
        false;


    rootdisplay_begin_update();


    editor_draw_document();

    editor_draw_status();


    rootdisplay_end_update();


    editor_selection_show();

    editor_place_cursor();
}


/*
 * ============================================================
 * REDRAW CURRENT LINE ONLY
 * ============================================================
 */

static void editor_redraw_current_line(void)
{
    editor_selection_hide();


    rootdisplay_begin_update();


    editor_draw_line(
        editor_line_number_from_index(
            editor_cursor
        )
    );


    editor_draw_status();


    rootdisplay_end_update();


    editor_selection_show();

    editor_place_cursor();
}


/*
 * ============================================================
 * MOVE CURSOR UPDATE
 * ============================================================
 */

static void editor_after_cursor_move(void)
{
    bool viewport_changed =
        editor_ensure_cursor_visible();


    if (
        viewport_changed
    )
    {
        editor_redraw_full();


        return;
    }


    editor_place_cursor();
}


/*
 * ============================================================
 * DELETE SELECTION
 * ============================================================
 */

static bool editor_delete_selection(void)
{
    if (
        !selection_active
    )
    {
        return false;
    }


    usize start =
        editor_selection_start();


    usize end =
        editor_selection_end();


    if (
        start == end
    )
    {
        editor_selection_clear();


        return false;
    }


    editor_selection_hide();


    bool structural =
        editor_range_has_newline(
            start,
            end
        );


    usize removed =
        end - start;


    for (
        usize i = start;
        i + removed < editor_length;
        i++
    )
    {
        editor_buffer[i] =
            editor_buffer[
                i + removed
            ];
    }


    editor_length -=
        removed;


    editor_cursor =
        start;


    editor_dirty =
        true;


    editor_selection_clear();


    return structural;
}


/*
 * ============================================================
 * INSERT
 * ============================================================
 */

static bool editor_insert(
    RootCodepoint codepoint
)
{
    bool structural =
        false;


    if (
        selection_active
    )
    {
        structural =
            editor_delete_selection();
    }


    if (
        editor_length
        >=
        ROOTEDIT_MAX_CODEPOINTS
    )
    {
        return structural;
    }


    for (
        usize i = editor_length;
        i > editor_cursor;
        i--
    )
    {
        editor_buffer[i] =
            editor_buffer[
                i - 1
            ];
    }


    editor_buffer[
        editor_cursor
    ] =
        codepoint;


    editor_cursor++;

    editor_length++;


    editor_dirty =
        true;


    editor_save_error =
        false;


    if (
        codepoint == '\n'
    )
    {
        structural =
            true;
    }


    return structural;
}


/*
 * ============================================================
 * BACKSPACE
 * ============================================================
 */

static bool editor_backspace(void)
{
    if (
        selection_active
    )
    {
        return
            editor_delete_selection();
    }


    if (
        editor_cursor == 0
    )
    {
        return false;
    }


    RootCodepoint removed =
        editor_buffer[
            editor_cursor - 1
        ];


    for (
        usize i =
            editor_cursor - 1;

        i + 1 < editor_length;

        i++
    )
    {
        editor_buffer[i] =
            editor_buffer[
                i + 1
            ];
    }


    editor_cursor--;

    editor_length--;


    editor_dirty =
        true;


    editor_save_error =
        false;


    return
        removed == '\n';
}


/*
 * ============================================================
 * DELETE
 * ============================================================
 */

static bool editor_delete(void)
{
    if (
        selection_active
    )
    {
        return
            editor_delete_selection();
    }


    if (
        editor_cursor
        >=
        editor_length
    )
    {
        return false;
    }


    RootCodepoint removed =
        editor_buffer[
            editor_cursor
        ];


    for (
        usize i = editor_cursor;
        i + 1 < editor_length;
        i++
    )
    {
        editor_buffer[i] =
            editor_buffer[
                i + 1
            ];
    }


    editor_length--;


    editor_dirty =
        true;


    editor_save_error =
        false;


    return
        removed == '\n';
}


/*
 * ============================================================
 * COPY SELECTION
 * ============================================================
 */

static void editor_copy_selection(void)
{
    if (
        !selection_active
    )
    {
        return;
    }


    usize start =
        editor_selection_start();


    usize end =
        editor_selection_end();


    editor_clipboard_length =
        end - start;


    for (
        usize i = 0;
        i < editor_clipboard_length;
        i++
    )
    {
        editor_clipboard[i] =
            editor_buffer[
                start + i
            ];
    }
}


/*
 * ============================================================
 * CUT SELECTION
 * ============================================================
 */

static bool editor_cut_selection(void)
{
    if (
        !selection_active
    )
    {
        return false;
    }


    editor_copy_selection();


    return
        editor_delete_selection();
}


/*
 * ============================================================
 * PASTE
 * ============================================================
 */

static bool editor_paste(void)
{
    if (
        editor_clipboard_length == 0
    )
    {
        return false;
    }


    bool structural =
        false;


    if (
        selection_active
    )
    {
        structural =
            editor_delete_selection();
    }


    if (
        editor_length
        +
        editor_clipboard_length
        >
        ROOTEDIT_MAX_CODEPOINTS
    )
    {
        return structural;
    }


    for (
        usize i = editor_length;
        i > editor_cursor;
        i--
    )
    {
        editor_buffer[
            i
            +
            editor_clipboard_length
            -
            1
        ] =
            editor_buffer[
                i - 1
            ];
    }


    for (
        usize i = 0;
        i < editor_clipboard_length;
        i++
    )
    {
        editor_buffer[
            editor_cursor + i
        ] =
            editor_clipboard[i];


        if (
            editor_clipboard[i]
            ==
            '\n'
        )
        {
            structural =
                true;
        }
    }


    editor_cursor +=
        editor_clipboard_length;


    editor_length +=
        editor_clipboard_length;


    editor_dirty =
        true;


    return structural;
}


/*
 * ============================================================
 * MOVE UP
 * ============================================================
 */

static void editor_move_up(void)
{
    usize current_start =
        editor_line_start_from_index(
            editor_cursor
        );


    if (
        current_start == 0
    )
    {
        return;
    }


    u32 column =
        editor_codepoint_column(
            editor_cursor
        );


    usize previous_end =
        current_start - 1;


    usize previous_start =
        editor_line_start_from_index(
            previous_end
        );


    usize previous_length =
        previous_end
        -
        previous_start;


    if (
        column > previous_length
    )
    {
        column =
            (u32)previous_length;
    }


    editor_cursor =
        previous_start
        +
        column;
}


/*
 * ============================================================
 * MOVE DOWN
 * ============================================================
 */

static void editor_move_down(void)
{
    usize current_end =
        editor_line_end_from_index(
            editor_cursor
        );


    if (
        current_end
        >=
        editor_length
    )
    {
        return;
    }


    u32 column =
        editor_codepoint_column(
            editor_cursor
        );


    usize next_start =
        current_end + 1;


    usize next_end =
        editor_line_end_from_index(
            next_start
        );


    usize next_length =
        next_end
        -
        next_start;


    if (
        column > next_length
    )
    {
        column =
            (u32)next_length;
    }


    editor_cursor =
        next_start
        +
        column;
}


/*
 * ============================================================
 * MOUSE -> BUFFER INDEX
 * ============================================================
 */

static bool editor_index_from_mouse(
    i32 mouse_x,
    i32 mouse_y,
    usize* result
)
{
    if (
        result == NULL
    )
    {
        return false;
    }


    u32 column;

    u32 row;


    if (
        !terminal_pixel_to_cell(
            mouse_x,
            mouse_y,
            &column,
            &row
        )
    )
    {
        return false;
    }


    if (
        row
        <
        ROOTEDIT_BODY_START_ROW
        ||
        row
        >=
        ROOTEDIT_BODY_START_ROW
        +
        editor_body_rows()
    )
    {
        return false;
    }


    u32 logical_line =
        editor_view_line
        +
        (
            row
            -
            ROOTEDIT_BODY_START_ROW
        );


    usize line_start =
        editor_line_start_by_number(
            logical_line
        );


    usize line_end =
        editor_line_end_from_index(
            line_start
        );


    u32 target =
        editor_horizontal_offset
        +
        column;


    u32 visual =
        0;


    usize position =
        line_start;


    while (
        position < line_end
    )
    {
        u32 width =
            terminal_codepoint_cells(
                editor_buffer[position]
            );


        if (
            target
            <
            visual
            +
            width
        )
        {
            break;
        }


        visual +=
            width;


        position++;
    }


    *result =
        position;


    return true;
}


/*
 * ============================================================
 * LOAD
 * ============================================================
 */

static FsResult editor_load(
    const char* path
)
{
    usize byte_count =
        0;


    FsResult result =
        filesystem_read_file(
            path,
            editor_file_bytes,
            sizeof(editor_file_bytes),
            &byte_count
        );


    if (
        result
        !=
        FS_RESULT_OK
    )
    {
        return result;
    }


    editor_length =
        0;


    editor_cursor =
        0;


    usize offset =
        0;


    while (
        offset < byte_count
        &&
        editor_length
        <
        ROOTEDIT_MAX_CODEPOINTS
    )
    {
        RootCodepoint codepoint;


        usize consumed =
            root_utf8_decode(
                editor_file_bytes
                +
                offset,

                byte_count
                -
                offset,

                &codepoint
            );


        if (
            consumed == 0
        )
        {
            codepoint =
                ROOT_UNICODE_REPLACEMENT;


            consumed =
                1;
        }


        editor_buffer[
            editor_length++
        ] =
            codepoint;


        offset +=
            consumed;
    }


    editor_cursor =
        editor_length;


    editor_dirty =
        false;


    editor_save_error =
        false;


    editor_view_line =
        0;


    editor_horizontal_offset =
        0;


    selection_active =
        false;


    selection_drawn =
        false;


    mouse_selecting =
        false;


    editor_clipboard_length =
        0;


    return
        FS_RESULT_OK;
}


/*
 * ============================================================
 * SAVE
 * ============================================================
 */

static FsResult editor_save(void)
{
    usize output =
        0;


    for (
        usize i = 0;
        i < editor_length;
        i++
    )
    {
        char encoded[4];


        usize count =
            root_utf8_encode(
                editor_buffer[i],
                encoded
            );


        if (
            output
            +
            count
            >
            FS_MAX_FILE_SIZE
        )
        {
            editor_save_error =
                true;


            return
                FS_RESULT_FILE_TOO_LARGE;
        }


        for (
            usize j = 0;
            j < count;
            j++
        )
        {
            editor_save_bytes[
                output++
            ] =
                encoded[j];
        }
    }


    FsResult result =
        filesystem_write_file(
            editor_path,
            editor_save_bytes,
            output
        );


    if (
        result
        ==
        FS_RESULT_OK
    )
    {
        editor_dirty =
            false;


        editor_save_error =
            false;
    }

    else
    {
        editor_save_error =
            true;
    }


    return result;
}


/*
 * ============================================================
 * ROOTEDIT
 * ============================================================
 */

FsResult rootedit_open(
    const char* path
)
{
    if (
        path == NULL
        ||
        path[0] == '\0'
    )
    {
        return
            FS_RESULT_INVALID_PATH;
    }


    if (
        root_strlcpy(
            editor_path,
            path,
            sizeof(editor_path)
        )
        >=
        sizeof(editor_path)
    )
    {
        return
            FS_RESULT_INVALID_PATH;
    }


    FsResult load_result =
        editor_load(
            path
        );


    if (
        load_result
        !=
        FS_RESULT_OK
    )
    {
        return load_result;
    }


    terminal_clear();


    rootdisplay_begin_update();


    editor_draw_header();

    editor_draw_document();

    editor_draw_status();


    rootdisplay_end_update();


    editor_place_cursor();


    bool running =
        true;


    while (
        running
    )
    {
        RootInputEvent event =
            rootinput_wait_event();


        /*
         * ====================================================
         * MOUSE BUTTON DOWN
         * ====================================================
         */

        if (
            event.type
            ==
            ROOT_INPUT_MOUSE_BUTTON_DOWN
            &&
            event.button
            ==
            ROOT_MOUSE_LEFT
        )
        {
            usize index;


            if (
                editor_index_from_mouse(
                    event.mouse_x,
                    event.mouse_y,
                    &index
                )
            )
            {
                editor_selection_hide();


                editor_cursor =
                    index;


                selection_anchor =
                    index;


                selection_active =
                    false;


                mouse_selecting =
                    true;


                editor_after_cursor_move();
            }


            continue;
        }


        /*
         * ====================================================
         * MOUSE DRAG = SELECT
         * ====================================================
         */

        if (
            event.type
            ==
            ROOT_INPUT_MOUSE_DRAG
            &&
            mouse_selecting
        )
        {
            usize index;


            if (
                editor_index_from_mouse(
                    event.mouse_x,
                    event.mouse_y,
                    &index
                )
            )
            {
                editor_selection_hide();


                editor_cursor =
                    index;


                selection_active =
                    editor_cursor
                    !=
                    selection_anchor;


                editor_ensure_cursor_visible();


                editor_selection_show();

                editor_place_cursor();
            }


            continue;
        }


        /*
         * ====================================================
         * MOUSE BUTTON UP
         * ====================================================
         */

        if (
            event.type
            ==
            ROOT_INPUT_MOUSE_BUTTON_UP
            &&
            event.button
            ==
            ROOT_MOUSE_LEFT
        )
        {
            mouse_selecting =
                false;


            continue;
        }


        /*
         * Ignorar movimientos normales.
         */

        if (
            event.type
            !=
            ROOT_INPUT_KEY_DOWN
        )
        {
            continue;
        }


        /*
         * ====================================================
         * CTRL COMMANDS
         * ====================================================
         */

        if (
            event.ctrl
        )
        {
            /*
             * Ctrl+S = save
             */

            if (
                event.key
                ==
                ROOT_KEY_S
            )
            {
                editor_save();


                rootdisplay_begin_update();

                editor_draw_status();

                rootdisplay_end_update();


                editor_place_cursor();


                continue;
            }


            /*
             * Ctrl+Q = exit
             */

            if (
                event.key
                ==
                ROOT_KEY_Q
            )
            {
                editor_selection_hide();


                running =
                    false;


                continue;
            }


            /*
             * Ctrl+A = select all
             */

            if (
                event.key
                ==
                ROOT_KEY_A
            )
            {
                editor_selection_hide();


                selection_anchor =
                    0;


                editor_cursor =
                    editor_length;


                selection_active =
                    editor_length != 0;


                editor_ensure_cursor_visible();

                editor_redraw_full();


                continue;
            }


            /*
             * Ctrl+C = copy
             */

            if (
                event.key
                ==
                ROOT_KEY_C
            )
            {
                editor_copy_selection();


                continue;
            }


            /*
             * Ctrl+K = cut
             */

            if (
                event.key
                ==
                ROOT_KEY_K
            )
            {
                bool structural =
                    editor_cut_selection();


                editor_ensure_cursor_visible();


                if (
                    structural
                )
                {
                    editor_redraw_full();
                }

                else
                {
                    editor_redraw_current_line();
                }


                continue;
            }


            /*
             * Ctrl+V = paste
             */

            if (
                event.key
                ==
                ROOT_KEY_V
            )
            {
                bool structural =
                    editor_paste();


                bool viewport_changed =
                    editor_ensure_cursor_visible();


                if (
                    structural
                    ||
                    viewport_changed
                )
                {
                    editor_redraw_full();
                }

                else
                {
                    editor_redraw_current_line();
                }


                continue;
            }
        }


        /*
         * ====================================================
         * SHIFT + ARROWS = SELECTION
         * ====================================================
         */

        if (
            event.shift
            &&
            (
                event.key == ROOT_KEY_LEFT
                ||
                event.key == ROOT_KEY_RIGHT
                ||
                event.key == ROOT_KEY_UP
                ||
                event.key == ROOT_KEY_DOWN
            )
        )
        {
            editor_selection_hide();


            if (
                !selection_active
            )
            {
                selection_anchor =
                    editor_cursor;
            }


            if (
                event.key == ROOT_KEY_LEFT
                &&
                editor_cursor > 0
            )
            {
                editor_cursor--;
            }


            if (
                event.key == ROOT_KEY_RIGHT
                &&
                editor_cursor < editor_length
            )
            {
                editor_cursor++;
            }


            if (
                event.key == ROOT_KEY_UP
            )
            {
                editor_move_up();
            }


            if (
                event.key == ROOT_KEY_DOWN
            )
            {
                editor_move_down();
            }


            selection_active =
                editor_cursor
                !=
                selection_anchor;


            bool viewport_changed =
                editor_ensure_cursor_visible();


            if (
                viewport_changed
            )
            {
                editor_redraw_full();
            }

            else
            {
                editor_selection_show();

                editor_place_cursor();
            }


            continue;
        }


        /*
         * ====================================================
         * NORMAL CURSOR MOVEMENT
         * ====================================================
         */

        if (
            event.key == ROOT_KEY_LEFT
        )
        {
            editor_selection_hide();

            editor_selection_clear();


            if (
                editor_cursor > 0
            )
            {
                editor_cursor--;
            }


            editor_after_cursor_move();

            continue;
        }


        if (
            event.key == ROOT_KEY_RIGHT
        )
        {
            editor_selection_hide();

            editor_selection_clear();


            if (
                editor_cursor < editor_length
            )
            {
                editor_cursor++;
            }


            editor_after_cursor_move();

            continue;
        }


        if (
            event.key == ROOT_KEY_UP
        )
        {
            editor_selection_hide();

            editor_selection_clear();


            editor_move_up();

            editor_after_cursor_move();

            continue;
        }


        if (
            event.key == ROOT_KEY_DOWN
        )
        {
            editor_selection_hide();

            editor_selection_clear();


            editor_move_down();

            editor_after_cursor_move();

            continue;
        }


        if (
            event.key == ROOT_KEY_HOME
        )
        {
            editor_selection_hide();

            editor_selection_clear();


            editor_cursor =
                editor_line_start_from_index(
                    editor_cursor
                );


            editor_after_cursor_move();

            continue;
        }


        if (
            event.key == ROOT_KEY_END
        )
        {
            editor_selection_hide();

            editor_selection_clear();


            editor_cursor =
                editor_line_end_from_index(
                    editor_cursor
                );


            editor_after_cursor_move();

            continue;
        }


        /*
         * ====================================================
         * BACKSPACE
         * ====================================================
         */

        if (
            event.key
            ==
            ROOT_KEY_BACKSPACE
        )
        {
            bool structural =
                editor_backspace();


            bool viewport_changed =
                editor_ensure_cursor_visible();


            if (
                structural
                ||
                viewport_changed
            )
            {
                editor_redraw_full();
            }

            else
            {
                editor_redraw_current_line();
            }


            continue;
        }


        /*
         * ====================================================
         * DELETE
         * ====================================================
         */

        if (
            event.key
            ==
            ROOT_KEY_DELETE
        )
        {
            bool structural =
                editor_delete();


            bool viewport_changed =
                editor_ensure_cursor_visible();


            if (
                structural
                ||
                viewport_changed
            )
            {
                editor_redraw_full();
            }

            else
            {
                editor_redraw_current_line();
            }


            continue;
        }


        /*
         * ====================================================
         * ENTER
         * ====================================================
         */

        if (
            event.key == ROOT_KEY_ENTER
            ||
            event.key == ROOT_KEY_KP_ENTER
        )
        {
            editor_insert(
                '\n'
            );


            editor_ensure_cursor_visible();

            editor_redraw_full();


            continue;
        }


        /*
         * ====================================================
         * TAB
         * ====================================================
         */

        if (
            event.key
            ==
            ROOT_KEY_TAB
        )
        {
            bool structural =
                false;


            for (
                u32 i = 0;
                i < 4;
                i++
            )
            {
                if (
                    editor_insert(' ')
                )
                {
                    structural =
                        true;
                }
            }


            bool viewport_changed =
                editor_ensure_cursor_visible();


            if (
                structural
                ||
                viewport_changed
            )
            {
                editor_redraw_full();
            }

            else
            {
                editor_redraw_current_line();
            }


            continue;
        }


        /*
         * ====================================================
         * TEXT
         * ====================================================
         */

        if (
            event.codepoint != 0
        )
        {
            bool structural =
                editor_insert(
                    event.codepoint
                );


            bool viewport_changed =
                editor_ensure_cursor_visible();


            /*
             * Caso normal:
             *
             * UNA tecla
             *      ↓
             * UNA sola línea redibujada.
             */

            if (
                structural
                ||
                viewport_changed
            )
            {
                editor_redraw_full();
            }

            else
            {
                editor_redraw_current_line();
            }


            continue;
        }
    }


    terminal_clear();


    return
        FS_RESULT_OK;
}