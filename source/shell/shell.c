#include "shell.h"

#include "terminal.h"
#include "rootinput.h"

#include "unicode.h"

#include "filesystem.h"
#include "path.h"

#include "io.h"

#include "string.h"

#include "system_config.h"


#define COMMAND_BUFFER_SIZE 128

#define COMMAND_UTF8_SIZE \
    ((COMMAND_BUFFER_SIZE * 4) + 1)


static RootCodepoint command_buffer[
    COMMAND_BUFFER_SIZE
];


static char command_utf8[
    COMMAND_UTF8_SIZE
];


static u32 command_length = 0;

static u32 command_cursor = 0;


/*
 * Celdas visuales, no bytes.
 */
static u32 rendered_length = 0;


static u32 input_start_row = 0;

static u32 input_start_col = 0;


/*
 * =====================================
 * COMANDOS
 * =====================================
 */

static void command_help(void)
{
    terminal_print(
        "Comandos disponibles:\n\n"
    );

    terminal_print(
        "help                  Mostrar comandos\n"
    );

    terminal_print(
        "clear                 Limpiar pantalla\n"
    );

    terminal_print(
        "echo <texto>          Mostrar texto\n"
    );

    terminal_print(
        "about                 Informacion del OS\n"
    );

    terminal_print(
        "godir <ruta>          Cambiar carpeta\n"
    );

    terminal_print(
        "seedir                Mostrar ruta actual\n"
    );

    terminal_print(
        "see                   Ver carpeta actual\n"
    );

    terminal_print(
        "see <ruta>            Ver otra carpeta\n"
    );

    terminal_print(
        "seedir(\"nombre\")      Buscar carpetas\n"
    );

    terminal_print(
        "reboot                Reiniciar\n"
    );

    terminal_print(
        "shutdown              Apagar QEMU\n"
    );

    terminal_print(
        "create --file <ruta>    Crear archivo\n"
    );

    terminal_print(
        "create --folder <ruta>  Crear carpeta\n"
    );

    terminal_print(
        "remove <ruta>           Eliminar archivo/carpeta\n"
    );

    terminal_print(
        "remove -r <ruta>        Eliminar recursivamente\n"
    );

    terminal_print(
        "copy <origen> <destino> Copiar\n"
    );

    terminal_print(
        "move <origen> <destino> Mover\n"
    );
}


static void command_about(void)
{
    terminal_print(
        ROOTOS_NAME
    );

    terminal_print(
        " v"
    );

    terminal_print(
        ROOTOS_VERSION_STRING
    );

    terminal_putchar('\n');


    terminal_print(
        "Build: "
    );

    terminal_print(
        ROOTOS_BUILD_TYPE
    );

    terminal_putchar('\n');


    terminal_print(
        "Architecture: x86 32-bit\n"
    );


    terminal_print(
        "Kernel: Root Kernel\n"
    );


    terminal_print(
        "Boot protocol: GRUB Multiboot\n"
    );


    terminal_print(
        "Default home: "
    );

    terminal_print(
        ROOTOS_DEFAULT_HOME
    );

    terminal_putchar('\n');
}


static void command_echo(
    const char* text
)
{
    terminal_print(text);

    terminal_putchar('\n');
}


/*
 * godir
 */
static void command_godir(
    const char* path
)
{
    if (
        filesystem_change_directory(
            path
        )
    )
    {
        return;
    }


    terminal_print(
        "No existe el directorio: "
    );

    terminal_print(path);

    terminal_putchar('\n');
}


/*
 * seedir
 */
static void command_seedir(void)
{
    filesystem_print_current_directory();
}


/*
 * see
 */
static void command_see(
    const char* path
)
{
    if (
        !filesystem_list(path)
    )
    {
        terminal_print(
            "Directorio no encontrado: "
        );

        terminal_print(path);

        terminal_putchar('\n');
    }
}


/*
 * seedir("nombre")
 */
static void command_find_directory(
    const char* command
)
{
    /*
     * Saltamos:
     *
     * seedir("
     *
     * Son 8 caracteres.
     */
    const char* text =
        command + 8;


    char name[64];

    u32 length = 0;


    /*
     * Copiar hasta encontrar ".
     */
    while (
        *text
        &&
        *text != '"'
        &&
        length < 63
    )
    {
        name[length] =
            *text;

        length++;

        text++;
    }


    name[length] = '\0';


    /*
     * Debemos tener:
     *
     * ")
     *
     * al final.
     */
    if (
        text[0] != '"'
        ||
        text[1] != ')'
        ||
        text[2] != '\0'
    )
    {
        terminal_print(
            "Uso: seedir(\"nombre\")\n"
        );

        return;
    }


    int found =
        filesystem_find_directories(
            name
        );


    if (found == 0)
    {
        terminal_print(
            "No se encontraron carpetas llamadas: "
        );

        terminal_print(name);

        terminal_putchar('\n');
    }
}


static void command_reboot(void)
{
    terminal_print(
        "Reiniciando...\n"
    );


    while (
        inb(0x64) & 0x02
    )
    {
    }


    outb(
        0x64,
        0xFE
    );


    while (1)
    {
        __asm__ volatile("hlt");
    }
}


static void command_shutdown(void)
{
    terminal_print(
        "Apagando...\n"
    );


    outw(
        0x604,
        0x2000
    );


    outw(
        0xB004,
        0x2000
    );


    while (1)
    {
        __asm__ volatile("hlt");
    }
}


static const char* shell_skip_spaces(
    const char* text
)
{
    while (
        *text == ' '
        ||
        *text == '\t'
    )
    {
        text++;
    }


    return text;
}



static bool shell_read_argument(
    const char** cursor,
    char* output,
    usize output_size
)
{
    const char* text =
        shell_skip_spaces(
            *cursor
        );


    if (
        *text == '\0'
    )
    {
        return false;
    }


    bool quoted =
        false;


    if (
        *text == '"'
    )
    {
        quoted = true;

        text++;
    }


    usize length = 0;


    while (*text)
    {
        if (
            quoted
        )
        {
            if (
                *text == '"'
            )
            {
                text++;

                break;
            }
        }

        else
        {
            if (
                *text == ' '
                ||
                *text == '\t'
            )
            {
                break;
            }
        }


        if (
            length
            >=
            output_size - 1
        )
        {
            return false;
        }


        output[
            length++
        ] =
            *text;


        text++;
    }


    output[
        length
    ] =
        '\0';


    *cursor =
        shell_skip_spaces(
            text
        );


    return
        length > 0;
}

static void shell_print_fs_result(
    FsResult result
)
{
    switch (result)
    {
        case FS_RESULT_OK:

            terminal_print(
                "Done.\n"
            );

            break;


        case FS_RESULT_NOT_FOUND:

            terminal_print(
                "Not found.\n"
            );

            break;


        case FS_RESULT_ALREADY_EXISTS:

            terminal_print(
                "Already exists.\n"
            );

            break;


        case FS_RESULT_NOT_DIRECTORY:

            terminal_print(
                "A path component is not a folder.\n"
            );

            break;


        case FS_RESULT_DIRECTORY_NOT_EMPTY:

            terminal_print(
                "Folder is not empty. Use --recursive.\n"
            );

            break;


        case FS_RESULT_INVALID_PATH:

            terminal_print(
                "Invalid path.\n"
            );

            break;


        case FS_RESULT_NO_SPACE:

            terminal_print(
                "Filesystem node table is full.\n"
            );

            break;


        case FS_RESULT_BUSY:

            terminal_print(
                "Resource is busy or protected.\n"
            );

            break;


        default:

            terminal_print(
                "Filesystem error.\n"
            );

            break;
    }
}

static void command_create(
    const char* arguments
)
{
    const char* cursor =
        arguments;


    char mode[32];

    char path[
        ROOT_PATH_MAX
    ];


    if (
        !shell_read_argument(
            &cursor,
            mode,
            sizeof(mode)
        )
        ||
        !shell_read_argument(
            &cursor,
            path,
            sizeof(path)
        )
    )
    {
        terminal_print(
            "Usage:\n"
            "  create --file <path>\n"
            "  create --folder <path>\n"
        );

        return;
    }


    if (
        root_streq(
            mode,
            "--file"
        )
    )
    {
        shell_print_fs_result(
            filesystem_create_file(
                path
            )
        );

        return;
    }


    if (
        root_streq(
            mode,
            "--folder"
        )
    )
    {
        shell_print_fs_result(
            filesystem_create_directory(
                path
            )
        );

        return;
    }


    terminal_print(
        "Unknown create mode.\n"
    );
}

static void command_remove(
    const char* arguments
)
{
    const char* cursor =
        arguments;


    char first[
        ROOT_PATH_MAX
    ];


    if (
        !shell_read_argument(
            &cursor,
            first,
            sizeof(first)
        )
    )
    {
        terminal_print(
            "Usage:\n"
            "  remove <path>\n"
            "  remove --recursive <path>\n"
        );

        return;
    }


    bool recursive =
        false;


    char path[
        ROOT_PATH_MAX
    ];


    if (
        root_streq(
            first,
            "--recursive"
        )
        ||
        root_streq(
            first,
            "-r"
        )
    )
    {
        recursive =
            true;


        if (
            !shell_read_argument(
                &cursor,
                path,
                sizeof(path)
            )
        )
        {
            terminal_print(
                "Missing path.\n"
            );

            return;
        }
    }

    else
    {
        root_strlcpy(
            path,
            first,
            ROOT_PATH_MAX
        );
    }


    shell_print_fs_result(
        filesystem_remove(
            path,
            recursive
        )
    );
}

static void command_copy(
    const char* arguments
)
{
    const char* cursor =
        arguments;


    char source[
        ROOT_PATH_MAX
    ];


    char destination[
        ROOT_PATH_MAX
    ];


    if (
        !shell_read_argument(
            &cursor,
            source,
            sizeof(source)
        )
        ||
        !shell_read_argument(
            &cursor,
            destination,
            sizeof(destination)
        )
    )
    {
        terminal_print(
            "Usage: copy <source> <destination>\n"
        );

        return;
    }


    shell_print_fs_result(
        filesystem_copy(
            source,
            destination
        )
    );
}

static void command_move(
    const char* arguments
)
{
    const char* cursor =
        arguments;


    char source[
        ROOT_PATH_MAX
    ];


    char destination[
        ROOT_PATH_MAX
    ];


    if (
        !shell_read_argument(
            &cursor,
            source,
            sizeof(source)
        )
        ||
        !shell_read_argument(
            &cursor,
            destination,
            sizeof(destination)
        )
    )
    {
        terminal_print(
            "Usage: move <source> <destination>\n"
        );

        return;
    }


    shell_print_fs_result(
        filesystem_move(
            source,
            destination
        )
    );
}

/*
 * =====================================
 * INTERPRETAR COMANDO
 * =====================================
 */

/*
 * =====================================
 * INTERPRETAR COMANDO
 * =====================================
 */

static void execute_command(
    const char* command
)
{
    /*
     * ============================================================
     * EMPTY COMMAND
     * ============================================================
     */

    if (
        command == NULL
        ||
        command[0] == '\0'
    )
    {
        return;
    }


    /*
     * ============================================================
     * HELP
     * ============================================================
     */

    if (
        root_streq(
            command,
            "help"
        )
    )
    {
        command_help();

        return;
    }


    /*
     * ============================================================
     * CLEAR
     * ============================================================
     */

    if (
        root_streq(
            command,
            "clear"
        )
    )
    {
        terminal_clear();

        return;
    }


    /*
     * ============================================================
     * ABOUT
     * ============================================================
     */

    if (
        root_streq(
            command,
            "about"
        )
    )
    {
        command_about();

        return;
    }


    /*
     * ============================================================
     * ECHO
     * ============================================================
     */

    if (
        root_streq(
            command,
            "echo"
        )
    )
    {
        terminal_putchar(
            '\n'
        );

        return;
    }


    if (
        root_starts_with(
            command,
            "echo "
        )
    )
    {
        command_echo(
            command + 5
        );

        return;
    }


    /*
     * ============================================================
     * GODIR
     * ============================================================
     */

    if (
        root_streq(
            command,
            "godir"
        )
    )
    {
        terminal_print(
            "Usage: godir <path>\n"
        );

        return;
    }


    if (
        root_starts_with(
            command,
            "godir "
        )
    )
    {
        command_godir(
            command + 6
        );

        return;
    }


    /*
     * ============================================================
     * SEEDIR("NAME")
     * ============================================================
     */

    if (
        root_starts_with(
            command,
            "seedir(\""
        )
    )
    {
        command_find_directory(
            command
        );

        return;
    }


    /*
     * ============================================================
     * SEEDIR
     * ============================================================
     */

    if (
        root_streq(
            command,
            "seedir"
        )
    )
    {
        command_seedir();

        return;
    }


    /*
     * ============================================================
     * SEE
     * ============================================================
     */

    if (
        root_streq(
            command,
            "see"
        )
    )
    {
        command_see(
            ""
        );

        return;
    }


    if (
        root_starts_with(
            command,
            "see "
        )
    )
    {
        command_see(
            command + 4
        );

        return;
    }


    /*
     * ============================================================
     * CREATE
     * ============================================================
     */

    if (
        root_streq(
            command,
            "create"
        )
    )
    {
        terminal_print(
            "Usage:\n"
            "  create --file <path>\n"
            "  create --folder <path>\n"
        );

        return;
    }


    if (
        root_starts_with(
            command,
            "create "
        )
    )
    {
        command_create(
            command + 7
        );

        return;
    }


    /*
     * ============================================================
     * REMOVE
     * ============================================================
     */

    if (
        root_streq(
            command,
            "remove"
        )
    )
    {
        terminal_print(
            "Usage:\n"
            "  remove <path>\n"
            "  remove --recursive <path>\n"
            "  remove -r <path>\n"
        );

        return;
    }


    if (
        root_starts_with(
            command,
            "remove "
        )
    )
    {
        command_remove(
            command + 7
        );

        return;
    }


    /*
     * ============================================================
     * COPY
     * ============================================================
     */

    if (
        root_streq(
            command,
            "copy"
        )
    )
    {
        terminal_print(
            "Usage: copy <source> <destination>\n"
        );

        return;
    }


    if (
        root_starts_with(
            command,
            "copy "
        )
    )
    {
        command_copy(
            command + 5
        );

        return;
    }


    /*
     * ============================================================
     * MOVE
     * ============================================================
     */

    if (
        root_streq(
            command,
            "move"
        )
    )
    {
        terminal_print(
            "Usage: move <source> <destination>\n"
        );

        return;
    }


    if (
        root_starts_with(
            command,
            "move "
        )
    )
    {
        command_move(
            command + 5
        );

        return;
    }


    /*
     * ============================================================
     * REBOOT
     * ============================================================
     */

    if (
        root_streq(
            command,
            "reboot"
        )
    )
    {
        command_reboot();

        return;
    }


    /*
     * ============================================================
     * SHUTDOWN
     * ============================================================
     */

    if (
        root_streq(
            command,
            "shutdown"
        )
    )
    {
        command_shutdown();

        return;
    }


    /*
     * ============================================================
     * UNKNOWN COMMAND
     * ============================================================
     */

    terminal_print(
        "Unknown command: "
    );

    terminal_print(
        command
    );

    terminal_putchar(
        '\n'
    );
}


static void shell_prompt(void)
{
    terminal_print(
        ROOTOS_DEFAULT_USER
    );

    terminal_putchar('@');

    terminal_print(
        ROOTOS_HOSTNAME
    );

    terminal_putchar(':');


    terminal_print(
        filesystem_current_directory()
    );


    terminal_print("$ ");


    if (
        terminal_get_col()
        >
        60
    )
    {
        terminal_putchar('\n');

        terminal_print("$ ");
    }


    input_start_row =
        terminal_get_row();

    input_start_col =
        terminal_get_col();

    rendered_length = 0;
}

/*
 * ============================================================
 * ANCHO VISUAL
 * ============================================================
 */

static u32 shell_cells_until(
    u32 index
)
{
    u32 cells = 0;


    if (
        index > command_length
    )
    {
        index =
            command_length;
    }


    for (
        u32 i = 0;
        i < index;
        i++
    )
    {
        cells +=
            terminal_codepoint_cells(
                command_buffer[i]
            );
    }


    return cells;
}


static u32 shell_total_cells(void)
{
    return
        shell_cells_until(
            command_length
        );
}


/*
 * ============================================================
 * CURSOR
 * ============================================================
 */

static void shell_update_cursor(void)
{
    terminal_set_cursor(
        input_start_col
        +
        shell_cells_until(
            command_cursor
        ),

        input_start_row
    );
}


/*
 * ============================================================
 * REDRAW
 * ============================================================
 */

static void shell_redraw_line(void)
{
    terminal_set_cursor(
        input_start_col,
        input_start_row
    );


    for (
        u32 i = 0;
        i < command_length;
        i++
    )
    {
        terminal_putcodepoint(
            command_buffer[i]
        );
    }


    u32 current_cells =
        shell_total_cells();


    for (
        u32 i = current_cells;
        i < rendered_length;
        i++
    )
    {
        terminal_putchar(
            ' '
        );
    }


    rendered_length =
        current_cells;


    shell_update_cursor();
}


/*
 * ============================================================
 * INSERTAR UNICODE
 * ============================================================
 */

static void shell_insert_codepoint(
    RootCodepoint codepoint
)
{
    if (
        !root_unicode_valid(
            codepoint
        )
    )
    {
        return;
    }


    if (
        command_length
        >=
        COMMAND_BUFFER_SIZE - 1
    )
    {
        return;
    }


    u32 needed =
        terminal_codepoint_cells(
            codepoint
        );


    u32 total =
        shell_total_cells();


    u32 columns =
        terminal_get_columns();


    /*
     * Por ahora:
     * editor de una sola línea.
     */
    if (
        input_start_col
        +
        total
        +
        needed
        >=
        columns
    )
    {
        return;
    }


    for (
        u32 i = command_length;
        i > command_cursor;
        i--
    )
    {
        command_buffer[i] =
            command_buffer[
                i - 1
            ];
    }


    command_buffer[
        command_cursor
    ] =
        codepoint;


    command_cursor++;

    command_length++;


    command_buffer[
        command_length
    ] =
        0;


    shell_redraw_line();
}


/*
 * ============================================================
 * BACKSPACE
 * ============================================================
 */

static void shell_backspace(void)
{
    if (
        command_cursor == 0
    )
    {
        return;
    }


    for (
        u32 i =
            command_cursor - 1;

        i <
            command_length - 1;

        i++
    )
    {
        command_buffer[i] =
            command_buffer[
                i + 1
            ];
    }


    command_cursor--;

    command_length--;


    command_buffer[
        command_length
    ] =
        0;


    shell_redraw_line();
}


/*
 * ============================================================
 * DELETE
 * ============================================================
 */

static void shell_delete(void)
{
    if (
        command_cursor
        >=
        command_length
    )
    {
        return;
    }


    for (
        u32 i =
            command_cursor;

        i <
            command_length - 1;

        i++
    )
    {
        command_buffer[i] =
            command_buffer[
                i + 1
            ];
    }


    command_length--;


    command_buffer[
        command_length
    ] =
        0;


    shell_redraw_line();
}


/*
 * ============================================================
 * CLEAR INPUT
 * ============================================================
 */

static void shell_clear_input(void)
{
    command_length = 0;

    command_cursor = 0;

    command_buffer[0] = 0;


    shell_redraw_line();
}


/*
 * ============================================================
 * UNICODE -> UTF-8 PARA LOS COMANDOS
 * ============================================================
 */

static bool shell_build_utf8(void)
{
    usize output = 0;


    for (
        u32 i = 0;
        i < command_length;
        i++
    )
    {
        char encoded[4];


        usize count =
            root_utf8_encode(
                command_buffer[i],
                encoded
            );


        if (
            output
            +
            count
            >=
            COMMAND_UTF8_SIZE
        )
        {
            return false;
        }


        for (
            usize j = 0;
            j < count;
            j++
        )
        {
            command_utf8[
                output++
            ] =
                encoded[j];
        }
    }


    command_utf8[
        output
    ] =
        '\0';


    return true;
}


/*
 * ============================================================
 * MOUSE -> CURSOR DE TEXTO
 * ============================================================
 */

static void shell_mouse_click(
    const RootInputEvent* event
)
{
    if (
        event->button
        !=
        ROOT_MOUSE_LEFT
    )
    {
        return;
    }


    u32 column;
    u32 row;


    if (
        !terminal_pixel_to_cell(
            event->mouse_x,
            event->mouse_y,
            &column,
            &row
        )
    )
    {
        return;
    }


    if (
        row
        !=
        input_start_row
    )
    {
        return;
    }


    if (
        column
        <
        input_start_col
    )
    {
        return;
    }


    u32 target =
        column
        -
        input_start_col;


    u32 position = 0;

    u32 cells = 0;


    while (
        position
        <
        command_length
    )
    {
        u32 width =
            terminal_codepoint_cells(
                command_buffer[
                    position
                ]
            );


        if (
            target
            <
            cells
            +
            width
        )
        {
            break;
        }


        cells +=
            width;

        position++;
    }


    command_cursor =
        position;


    shell_update_cursor();
}


/*
 * ============================================================
 * SHELL LOOP
 * ============================================================
 */

void shell_run(void)
{
    command_length = 0;

    command_cursor = 0;

    rendered_length = 0;

    command_buffer[0] = 0;


    shell_prompt();


    while (1)
    {
        RootInputEvent event =
            rootinput_wait_event();


        /*
         * ========================================
         * MOUSE
         * ========================================
         */

        if (
            event.type
            ==
            ROOT_INPUT_MOUSE_CLICK
        )
        {
            shell_mouse_click(
                &event
            );

            continue;
        }


        /*
         * Solo KEY DOWN afecta
         * actualmente a la shell.
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
         * ========================================
         * CTRL SHORTCUTS
         * ========================================
         */

        if (event.ctrl)
        {
            /*
             * Ctrl+A
             */
            if (
                event.key
                ==
                ROOT_KEY_A
            )
            {
                command_cursor = 0;

                shell_update_cursor();

                continue;
            }


            /*
             * Ctrl+E
             */
            if (
                event.key
                ==
                ROOT_KEY_E
            )
            {
                command_cursor =
                    command_length;

                shell_update_cursor();

                continue;
            }


            /*
             * Ctrl+U
             */
            if (
                event.key
                ==
                ROOT_KEY_U
            )
            {
                shell_clear_input();

                continue;
            }


            /*
             * Ctrl+L
             */
            if (
                event.key
                ==
                ROOT_KEY_L
            )
            {
                terminal_clear();

                shell_prompt();

                shell_redraw_line();

                continue;
            }


            /*
             * Ctrl+C
             */
            if (
                event.key
                ==
                ROOT_KEY_C
            )
            {
                terminal_print(
                    "^C\n"
                );


                command_length = 0;

                command_cursor = 0;

                rendered_length = 0;

                command_buffer[0] = 0;


                shell_prompt();

                continue;
            }
        }


        /*
         * ========================================
         * ENTER
         * ========================================
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
            command_cursor =
                command_length;


            shell_update_cursor();


            terminal_putchar(
                '\n'
            );


            if (
                shell_build_utf8()
            )
            {
                execute_command(
                    command_utf8
                );
            }


            command_length = 0;

            command_cursor = 0;

            rendered_length = 0;

            command_buffer[0] = 0;


            shell_prompt();

            continue;
        }


        /*
         * ========================================
         * MOVIMIENTO
         * ========================================
         */

        if (
            event.key
            ==
            ROOT_KEY_LEFT
        )
        {
            if (
                command_cursor > 0
            )
            {
                command_cursor--;

                shell_update_cursor();
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
                command_cursor
                <
                command_length
            )
            {
                command_cursor++;

                shell_update_cursor();
            }


            continue;
        }


        if (
            event.key
            ==
            ROOT_KEY_HOME
        )
        {
            command_cursor = 0;

            shell_update_cursor();

            continue;
        }


        if (
            event.key
            ==
            ROOT_KEY_END
        )
        {
            command_cursor =
                command_length;

            shell_update_cursor();

            continue;
        }


        /*
         * ========================================
         * BORRAR
         * ========================================
         */

        if (
            event.key
            ==
            ROOT_KEY_BACKSPACE
        )
        {
            shell_backspace();

            continue;
        }


        if (
            event.key
            ==
            ROOT_KEY_DELETE
        )
        {
            shell_delete();

            continue;
        }


        /*
         * ========================================
         * TEXTO UNICODE
         * ========================================
         */

        if (
            event.codepoint
            !=
            0
        )
        {
            shell_insert_codepoint(
                event.codepoint
            );

            continue;
        }
    }
}
