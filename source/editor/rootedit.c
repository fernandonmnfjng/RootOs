#include "rootedit.h"

#include "terminal.h"
#include "rootinput.h"

#include "unicode.h"

#include "string.h"
#include "memory.h"

#include "path.h"


#define ROOTEDIT_MAX_CODEPOINTS FS_MAX_FILE_SIZE

#define ROOTEDIT_BODY_START_ROW 3


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
        editor_buffer[
            index
        ]
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


    u32 current_line =
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
            current_line++;


            if (
                current_line
                ==
                line
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
            index - start
        );
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


/*
 * ============================================================
 * CURSOR MOVEMENT
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
        column
        >
        previous_length
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
        column
        >
        next_length
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
 * EDIT BUFFER
 * ============================================================
 */

static void editor_insert(
    RootCodepoint codepoint
)
{
    if (
        editor_length
        >=
        ROOTEDIT_MAX_CODEPOINTS
    )
    {
        return;
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
}


static void editor_backspace(void)
{
    if (
        editor_cursor == 0
    )
    {
        return;
    }


    for (
        usize i =
            editor_cursor - 1;

        i + 1
        <
        editor_length;

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
}


static void editor_delete(void)
{
    if (
        editor_cursor
        >=
        editor_length
    )
    {
        return;
    }


    for (
        usize i =
            editor_cursor;

        i + 1
        <
        editor_length;

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
 * DRAW SEPARATOR
 * ============================================================
 */

static void editor_draw_separator(
    u32 columns
)
{
    for (
        u32 i = 0;
        i < columns;
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
 * RENDER
 * ============================================================
 */

static void editor_redraw(void)
{
    terminal_clear();


    u32 columns =
        terminal_get_columns();


    u32 rows =
        terminal_get_rows();


    if (
        columns == 0
    )
    {
        columns =
            80;
    }


    if (
        rows < 5
    )
    {
        rows =
            25;
    }


    u32 body_rows =
        rows
        -
        4;


    u32 cursor_line =
        editor_line_number_from_index(
            editor_cursor
        );


    /*
     * Vertical viewport.
     */

    if (
        cursor_line
        <
        editor_view_line
    )
    {
        editor_view_line =
            cursor_line;
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
    }


    /*
     * Horizontal viewport.
     */

    usize cursor_line_start =
        editor_line_start_from_index(
            editor_cursor
        );


    u32 cursor_visual_column =
        editor_visual_column(
            cursor_line_start,
            editor_cursor
        );


    if (
        cursor_visual_column
        <
        editor_horizontal_offset
    )
    {
        editor_horizontal_offset =
            cursor_visual_column;
    }


    if (
        cursor_visual_column
        >=
        editor_horizontal_offset
        +
        columns
    )
    {
        editor_horizontal_offset =
            cursor_visual_column
            -
            columns
            +
            1;
    }


    /*
     * Header.
     */

    terminal_set_cursor(
        0,
        0
    );


    terminal_print(
        "RootEdit - "
    );


    terminal_print(
        editor_path
    );


    terminal_set_cursor(
        0,
        1
    );


    terminal_print(
        "Ctrl+S Guardar | Ctrl+Q Salir | Ctrl+X Guardar+Salir"
    );


    terminal_set_cursor(
        0,
        2
    );


    editor_draw_separator(
        columns
    );


    /*
     * Body.
     */

    usize line_start =
        editor_line_start_by_number(
            editor_view_line
        );


    for (
        u32 screen_line = 0;
        screen_line < body_rows;
        screen_line++
    )
    {
        terminal_set_cursor(
            0,
            ROOTEDIT_BODY_START_ROW
            +
            screen_line
        );


        if (
            line_start
            >
            editor_length
        )
        {
            break;
        }


        usize line_end =
            editor_line_end_from_index(
                line_start
            );


        u32 logical_visual_column =
            0;


        u32 output_cells =
            0;


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
                logical_visual_column
                +
                width
                <=
                editor_horizontal_offset
            )
            {
                logical_visual_column +=
                    width;


                continue;
            }


            if (
                output_cells
                +
                width
                >
                columns
            )
            {
                break;
            }


            terminal_putcodepoint(
                codepoint
            );


            logical_visual_column +=
                width;


            output_cells +=
                width;
        }


        if (
            line_end
            >=
            editor_length
        )
        {
            line_start =
                editor_length + 1;
        }

        else
        {
            line_start =
                line_end + 1;
        }
    }


    /*
     * Status.
     */

    terminal_set_cursor(
        0,
        rows - 1
    );


    if (
        editor_save_error
    )
    {
        terminal_print(
            "[ERROR AL GUARDAR]"
        );
    }

    else if (
        editor_dirty
    )
    {
        terminal_print(
            "[MODIFICADO]"
        );
    }

    else
    {
        terminal_print(
            "[GUARDADO]"
        );
    }


    /*
     * Cursor de texto.
     */

    u32 cursor_x =
        cursor_visual_column
        -
        editor_horizontal_offset;


    u32 cursor_y =
        ROOTEDIT_BODY_START_ROW
        +
        (
            cursor_line
            -
            editor_view_line
        );


    if (
        cursor_x
        >=
        columns
    )
    {
        cursor_x =
            columns - 1;
    }


    if (
        cursor_y
        >=
        rows - 1
    )
    {
        cursor_y =
            rows - 2;
    }


    terminal_set_cursor(
        cursor_x,
        cursor_y
    );
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
        return
            load_result;
    }


    bool running =
        true;


    while (
        running
    )
    {
        editor_redraw();


        RootInputEvent event =
            rootinput_wait_event();


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
            if (
                event.key
                ==
                ROOT_KEY_S
            )
            {
                editor_save();

                continue;
            }


            if (
                event.key
                ==
                ROOT_KEY_Q
            )
            {
                running =
                    false;


                continue;
            }


            if (
                event.key
                ==
                ROOT_KEY_X
            )
            {
                if (
                    editor_save()
                    ==
                    FS_RESULT_OK
                )
                {
                    running =
                        false;
                }


                continue;
            }
        }


        /*
         * ====================================================
         * MOVEMENT
         * ====================================================
         */

        if (
            event.key
            ==
            ROOT_KEY_LEFT
        )
        {
            if (
                editor_cursor > 0
            )
            {
                editor_cursor--;
            }


            continue;
        }


        if (
            event.key
            ==
            ROOT_KEY_RIGHT
        )
        {
            if (
                editor_cursor
                <
                editor_length
            )
            {
                editor_cursor++;
            }


            continue;
        }


        if (
            event.key
            ==
            ROOT_KEY_UP
        )
        {
            editor_move_up();

            continue;
        }


        if (
            event.key
            ==
            ROOT_KEY_DOWN
        )
        {
            editor_move_down();

            continue;
        }


        if (
            event.key
            ==
            ROOT_KEY_HOME
        )
        {
            editor_cursor =
                editor_line_start_from_index(
                    editor_cursor
                );


            continue;
        }


        if (
            event.key
            ==
            ROOT_KEY_END
        )
        {
            editor_cursor =
                editor_line_end_from_index(
                    editor_cursor
                );


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
            ROOT_KEY_BACKSPACE
        )
        {
            editor_backspace();

            continue;
        }


        if (
            event.key
            ==
            ROOT_KEY_DELETE
        )
        {
            editor_delete();

            continue;
        }


        /*
         * ====================================================
         * ENTER
         * ====================================================
         */

        if (
            event.key
            ==
            ROOT_KEY_ENTER
            ||
            event.key
            ==
            ROOT_KEY_KP_ENTER
        )
        {
            editor_insert(
                '\n'
            );


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
            editor_insert(' ');
            editor_insert(' ');
            editor_insert(' ');
            editor_insert(' ');

            continue;
        }


        /*
         * ====================================================
         * UNICODE TEXT
         * ====================================================
         */

        if (
            event.codepoint != 0
        )
        {
            editor_insert(
                event.codepoint
            );


            continue;
        }
    }


    terminal_clear();


    return
        FS_RESULT_OK;
}