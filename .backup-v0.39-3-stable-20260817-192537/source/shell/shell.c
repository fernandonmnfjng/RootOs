#include "shell.h"

#include "terminal.h"
#include "rootinput.h"
#include "rootclipboard.h"
#include "roottext.h"
#include "rootedit.h"

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
 * ============================================================
 * COMMAND HISTORY
 * ============================================================
 */

#define SHELL_HISTORY_SIZE 32


static RootCodepoint command_history[
    SHELL_HISTORY_SIZE
][
    COMMAND_BUFFER_SIZE
];


static u32 command_history_length[
    SHELL_HISTORY_SIZE
];


static u32 command_history_count =
    0;


static i32 command_history_position =
    -1;

/*
 * =====================================
 * COMANDOS
 * =====================================
 */

static void command_help(void)
{
    terminal_print(
        "RootOS commands:\n\n"
    );


    terminal_print(
        "help                         Show commands\n"
    );


    terminal_print(
        "clear                        Clear terminal\n"
    );


    terminal_print(
        "about                        Show RootOS information\n"
    );


    terminal_print(
        "echo <text>                  Print text\n"
    );


    terminal_print(
        "godir <path>                 Change directory\n"
    );


    terminal_print(
        "seedir                       Show current directory\n"
    );


    terminal_print(
        "seedir(\"name\")               Find directories\n"
    );


    terminal_print(
        "see                           List current directory\n"
    );


    terminal_print(
        "see <path>                    List directory\n"
    );


    terminal_print(
        "create --file <path>          Create file\n"
    );


    terminal_print(
        "create --folder <path>        Create folder\n"
    );


    terminal_print(
        "remove <path>                 Remove file/folder\n"
    );


    terminal_print(
        "remove -r <path>              Remove recursively\n"
    );


    terminal_print(
        "copy <source> <destination>   Copy\n"
    );


    terminal_print(
        "move <source> <destination>   Move\n"
    );


    terminal_print(
        "readfile <path>               Read text file\n"
    );


    terminal_print(
        "writefile <path> \"text\"      Replace file content\n"
    );


    terminal_print(
        "appendfile <path> \"text\"     Append file content\n"
    );


    terminal_print(
        "editfile <path>               Open RootEdit\n"
    );


    terminal_print(
        "reboot                        Reboot\n"
    );


    terminal_print(
        "shutdown                      Shutdown QEMU\n"
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
    switch (
        result
    )
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
                "No space available.\n"
            );

            break;


        case FS_RESULT_BUSY:

            terminal_print(
                "Resource is busy or protected.\n"
            );

            break;


        case FS_RESULT_NOT_FILE:

            terminal_print(
                "Path is not a file.\n"
            );

            break;


        case FS_RESULT_FILE_TOO_LARGE:

            terminal_print(
                "File is too large.\n"
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
 * ============================================================
 * READFILE
 * ============================================================
 */

static void command_readfile(
    const char* arguments
)
{
    const char* cursor =
        arguments;


    char path[
        ROOT_PATH_MAX
    ];


    if (
        !shell_read_argument(
            &cursor,
            path,
            sizeof(path)
        )
    )
    {
        terminal_print(
            "Usage: readfile <path>\n"
        );

        return;
    }


    static char file_buffer[
        FS_MAX_FILE_SIZE + 1
    ];


    usize size =
        0;


    FsResult result =
        filesystem_read_file(
            path,
            file_buffer,
            sizeof(file_buffer),
            &size
        );


    if (
        result
        !=
        FS_RESULT_OK
    )
    {
        shell_print_fs_result(
            result
        );

        return;
    }


    terminal_print(
        file_buffer
    );


    if (
        size == 0
        ||
        file_buffer[
            size - 1
        ]
        !=
        '\n'
    )
    {
        terminal_putchar(
            '\n'
        );
    }
}


/*
 * ============================================================
 * WRITEFILE
 * ============================================================
 */

static void command_writefile(
    const char* arguments
)
{
    const char* cursor =
        arguments;


    char path[
        ROOT_PATH_MAX
    ];


    char text[
        FS_MAX_FILE_SIZE + 1
    ];


    if (
        !shell_read_argument(
            &cursor,
            path,
            sizeof(path)
        )
        ||
        !shell_read_argument(
            &cursor,
            text,
            sizeof(text)
        )
    )
    {
        terminal_print(
            "Usage: writefile <path> \"text\"\n"
        );

        return;
    }


    shell_print_fs_result(
        filesystem_write_file(
            path,
            text,
            root_strlen(
                text
            )
        )
    );
}


/*
 * ============================================================
 * APPENDFILE
 * ============================================================
 */

static void command_appendfile(
    const char* arguments
)
{
    const char* cursor =
        arguments;


    char path[
        ROOT_PATH_MAX
    ];


    char text[
        FS_MAX_FILE_SIZE + 1
    ];


    if (
        !shell_read_argument(
            &cursor,
            path,
            sizeof(path)
        )
        ||
        !shell_read_argument(
            &cursor,
            text,
            sizeof(text)
        )
    )
    {
        terminal_print(
            "Usage: appendfile <path> \"text\"\n"
        );

        return;
    }


    shell_print_fs_result(
        filesystem_append_file(
            path,
            text,
            root_strlen(
                text
            )
        )
    );
}


/*
 * ============================================================
 * EDITFILE
 * ============================================================
 */

static void command_editfile(
    const char* arguments
)
{
    const char* cursor =
        arguments;


    char path[
        ROOT_PATH_MAX
    ];


    if (
        !shell_read_argument(
            &cursor,
            path,
            sizeof(path)
        )
    )
    {
        terminal_print(
            "Usage: editfile <path>\n"
        );

        return;
    }


    FsResult result =
        rootedit_open(
            path
        );


    if (
        result
        !=
        FS_RESULT_OK
    )
    {
        shell_print_fs_result(
            result
        );
    }
}

/*
 * =====================================
 * INTERPRETAR COMANDO
 * =====================================
 */

static void execute_command(
    const char* command
)
{
    if (
        command == NULL
        ||
        command[0] == '\0'
    )
    {
        return;
    }


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
     * SEEDIR SEARCH
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
     * READFILE
     * ============================================================
     */

    if (
        root_streq(
            command,
            "readfile"
        )
    )
    {
        terminal_print(
            "Usage: readfile <path>\n"
        );

        return;
    }


    if (
        root_starts_with(
            command,
            "readfile "
        )
    )
    {
        command_readfile(
            command + 9
        );

        return;
    }


    /*
     * ============================================================
     * WRITEFILE
     * ============================================================
     */

    if (
        root_streq(
            command,
            "writefile"
        )
    )
    {
        terminal_print(
            "Usage: writefile <path> \"text\"\n"
        );

        return;
    }


    if (
        root_starts_with(
            command,
            "writefile "
        )
    )
    {
        command_writefile(
            command + 10
        );

        return;
    }


    /*
     * ============================================================
     * APPENDFILE
     * ============================================================
     */

    if (
        root_streq(
            command,
            "appendfile"
        )
    )
    {
        terminal_print(
            "Usage: appendfile <path> \"text\"\n"
        );

        return;
    }


    if (
        root_starts_with(
            command,
            "appendfile "
        )
    )
    {
        command_appendfile(
            command + 11
        );

        return;
    }


    /*
     * ============================================================
     * EDITFILE
     * ============================================================
     */

    if (
        root_streq(
            command,
            "editfile"
        )
    )
    {
        terminal_print(
            "Usage: editfile <path>\n"
        );

        return;
    }


    if (
        root_starts_with(
            command,
            "editfile "
        )
    )
    {
        command_editfile(
            command + 9
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
     * UNKNOWN
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
 * HISTORY - LOAD BUFFER
 * ============================================================
 */

static void shell_history_load(
    u32 history_index
)
{
    if (
        history_index
        >=
        command_history_count
    )
    {
        return;
    }


    command_length =
        command_history_length[
            history_index
        ];


    for (
        u32 i = 0;
        i < command_length;
        i++
    )
    {
        command_buffer[i] =
            command_history[
                history_index
            ][
                i
            ];
    }


    command_buffer[
        command_length
    ] =
        0;


    command_cursor =
        command_length;


    shell_redraw_line();
}


/*
 * ============================================================
 * HISTORY - ADD CURRENT COMMAND
 * ============================================================
 */

static void shell_history_add_current(void)
{
    if (
        command_length == 0
    )
    {
        command_history_position =
            -1;


        return;
    }


    /*
     * Evitar guardar dos comandos
     * consecutivos idénticos.
     */

    if (
        command_history_count > 0
    )
    {
        u32 last =
            command_history_count - 1;


        if (
            command_history_length[last]
            ==
            command_length
        )
        {
            bool equal =
                true;


            for (
                u32 i = 0;
                i < command_length;
                i++
            )
            {
                if (
                    command_history[last][i]
                    !=
                    command_buffer[i]
                )
                {
                    equal =
                        false;


                    break;
                }
            }


            if (
                equal
            )
            {
                command_history_position =
                    -1;


                return;
            }
        }
    }


    /*
     * Cola llena:
     * eliminar comando más antiguo.
     */

    if (
        command_history_count
        >=
        SHELL_HISTORY_SIZE
    )
    {
        for (
            u32 i = 1;
            i < SHELL_HISTORY_SIZE;
            i++
        )
        {
            command_history_length[
                i - 1
            ] =
                command_history_length[
                    i
                ];


            for (
                u32 j = 0;
                j <
                command_history_length[i];

                j++
            )
            {
                command_history[
                    i - 1
                ][
                    j
                ] =
                    command_history[
                        i
                    ][
                        j
                    ];
            }
        }


        command_history_count =
            SHELL_HISTORY_SIZE - 1;
    }


    u32 index =
        command_history_count;


    command_history_length[
        index
    ] =
        command_length;


    for (
        u32 i = 0;
        i < command_length;
        i++
    )
    {
        command_history[
            index
        ][
            i
        ] =
            command_buffer[i];
    }


    command_history_count++;


    command_history_position =
        -1;
}


/*
 * ============================================================
 * HISTORY - PREVIOUS
 * ============================================================
 */

static void shell_history_previous(void)
{
    if (
        command_history_count == 0
    )
    {
        return;
    }


    if (
        command_history_position < 0
    )
    {
        command_history_position =
            (i32)command_history_count
            -
            1;
    }

    else if (
        command_history_position > 0
    )
    {
        command_history_position--;
    }


    shell_history_load(
        (u32)command_history_position
    );
}


/*
 * ============================================================
 * HISTORY - NEXT
 * ============================================================
 */

static void shell_history_next(void)
{
    if (
        command_history_position < 0
    )
    {
        return;
    }


    if (
        command_history_position
        <
        (i32)command_history_count - 1
    )
    {
        command_history_position++;


        shell_history_load(
            (u32)command_history_position
        );


        return;
    }


    command_history_position =
        -1;


    command_length =
        0;


    command_cursor =
        0;


    command_buffer[0] =
        0;


    shell_redraw_line();
}

/*
 * ============================================================
 * INSERTAR UNICODE
 * ============================================================
 */

static bool shell_insert_codepoint_raw(
    RootCodepoint codepoint
)
{
    if (!root_unicode_valid(codepoint))
        return false;

    if (command_length >= COMMAND_BUFFER_SIZE - 1)
        return false;

    u32 needed = terminal_codepoint_cells(codepoint);
    u32 total = shell_total_cells();
    u32 columns = terminal_get_columns();

    /* Shell input is intentionally one visual line for now. */
    if (
        input_start_col + total + needed >= columns
    )
    {
        return false;
    }

    for (u32 i = command_length; i > command_cursor; i--)
        command_buffer[i] = command_buffer[i - 1];

    command_buffer[command_cursor] = codepoint;
    command_cursor++;
    command_length++;
    command_buffer[command_length] = 0;
    return true;
}


static void shell_insert_codepoint(
    RootCodepoint codepoint
)
{
    if (shell_insert_codepoint_raw(codepoint))
        shell_redraw_line();
}


static void shell_paste_clipboard(void)
{
    const RootCodepoint* data = rootclipboard_data();
    usize length = rootclipboard_length();

    if (data == NULL || length == 0)
        return;

    bool changed = false;

    for (usize i = 0; i < length; i++)
    {
        RootCodepoint codepoint = data[i];

        /* Current shell editor is single-line. */
        if (codepoint == '\n' || codepoint == '\r')
            codepoint = ' ';

        if (!shell_insert_codepoint_raw(codepoint))
            break;

        changed = true;
    }

    if (changed)
    {
        command_history_position = -1;
        shell_redraw_line();
    }
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

    command_history_count = 0;
    command_history_position = -1;

    shell_prompt();

    while (1)
    {
        RootInputEvent event = rootinput_wait_event();

        /* ========================================================
         * TERMINAL MOUSE SELECTION
         * ======================================================== */

        if (
            event.type == ROOT_INPUT_MOUSE_BUTTON_DOWN &&
            event.button == ROOT_MOUSE_LEFT
        )
        {
            terminal_selection_begin(
                event.mouse_x,
                event.mouse_y
            );

            continue;
        }

        if (event.type == ROOT_INPUT_MOUSE_DRAG)
        {
            terminal_selection_drag(
                event.mouse_x,
                event.mouse_y
            );

            continue;
        }

        if (
            event.type == ROOT_INPUT_MOUSE_BUTTON_UP &&
            event.button == ROOT_MOUSE_LEFT
        )
        {
            terminal_selection_end();
            continue;
        }

        if (
            event.type == ROOT_INPUT_MOUSE_DOUBLE_CLICK &&
            event.button == ROOT_MOUSE_LEFT
        )
        {
            terminal_selection_select_word(
                event.mouse_x,
                event.mouse_y
            );

            continue;
        }

        if (event.type == ROOT_INPUT_MOUSE_CLICK)
        {
            /* A plain click in the active command line moves its caret. */
            if (!terminal_selection_active())
                shell_mouse_click(&event);

            continue;
        }

        if (event.type != ROOT_INPUT_KEY_DOWN)
            continue;

        /* ========================================================
         * STANDARD TERMINAL SCROLLBACK
         * ======================================================== */

        if (
            event.shift &&
            !event.ctrl &&
            !event.alt &&
            event.key == ROOT_KEY_PAGE_UP
        )
        {
            u32 amount = terminal_get_rows() / 2u;
            if (amount == 0)
                amount = 1;

            terminal_scrollback_up(amount);
            continue;
        }

        if (
            event.shift &&
            !event.ctrl &&
            !event.alt &&
            event.key == ROOT_KEY_PAGE_DOWN
        )
        {
            u32 amount = terminal_get_rows() / 2u;
            if (amount == 0)
                amount = 1;

            terminal_scrollback_down(amount);
            continue;
        }

        RootTextAction action = roottext_terminal_action(&event);

        /* Ctrl+Shift+C copies without returning from scrollback view. */
        if (action == ROOT_TEXT_ACTION_TERMINAL_COPY)
        {
            terminal_selection_copy();
            continue;
        }

        if (action == ROOT_TEXT_ACTION_TERMINAL_PASTE)
        {
            terminal_scrollback_bottom();
            terminal_selection_clear();
            shell_paste_clipboard();
            continue;
        }

        /* Keyboard editing always returns to the live prompt. */
        terminal_scrollback_bottom();

        if (action == ROOT_TEXT_ACTION_LINE_START)
        {
            command_cursor = 0;
            shell_update_cursor();
            continue;
        }

        if (action == ROOT_TEXT_ACTION_LINE_END)
        {
            command_cursor = command_length;
            shell_update_cursor();
            continue;
        }

        if (action == ROOT_TEXT_ACTION_LINE_CLEAR)
        {
            shell_clear_input();
            command_history_position = -1;
            continue;
        }

        if (action == ROOT_TEXT_ACTION_CLEAR_SCREEN)
        {
            terminal_clear();
            shell_prompt();
            shell_redraw_line();
            continue;
        }

        if (action == ROOT_TEXT_ACTION_INTERRUPT)
        {
            terminal_print("^C\n");

            command_length = 0;
            command_cursor = 0;
            rendered_length = 0;
            command_buffer[0] = 0;
            command_history_position = -1;

            shell_prompt();
            continue;
        }

        if (action == ROOT_TEXT_ACTION_SUSPEND)
        {
            /* Reserved for job control once processes exist. */
            continue;
        }

        /* Any other Ctrl/Alt combination is not text. */
        if (event.ctrl || (event.alt && !event.altgr))
            continue;

        /* ========================================================
         * HISTORY
         * ======================================================== */

        if (event.key == ROOT_KEY_UP)
        {
            shell_history_previous();
            continue;
        }

        if (event.key == ROOT_KEY_DOWN)
        {
            shell_history_next();
            continue;
        }

        /* ========================================================
         * ENTER
         * ======================================================== */

        if (
            event.key == ROOT_KEY_ENTER ||
            event.key == ROOT_KEY_KP_ENTER
        )
        {
            command_cursor = command_length;
            shell_update_cursor();
            terminal_putchar('\n');

            shell_history_add_current();

            if (shell_build_utf8())
                execute_command(command_utf8);

            command_length = 0;
            command_cursor = 0;
            rendered_length = 0;
            command_buffer[0] = 0;
            command_history_position = -1;

            shell_prompt();
            continue;
        }

        /* ========================================================
         * CURSOR MOVEMENT
         * ======================================================== */

        if (event.key == ROOT_KEY_LEFT)
        {
            if (command_cursor > 0)
            {
                command_cursor--;
                shell_update_cursor();
            }

            continue;
        }

        if (event.key == ROOT_KEY_RIGHT)
        {
            if (command_cursor < command_length)
            {
                command_cursor++;
                shell_update_cursor();
            }

            continue;
        }

        if (event.key == ROOT_KEY_HOME)
        {
            command_cursor = 0;
            shell_update_cursor();
            continue;
        }

        if (event.key == ROOT_KEY_END)
        {
            command_cursor = command_length;
            shell_update_cursor();
            continue;
        }

        /* ========================================================
         * DELETE
         * ======================================================== */

        if (event.key == ROOT_KEY_BACKSPACE)
        {
            command_history_position = -1;
            shell_backspace();
            continue;
        }

        if (event.key == ROOT_KEY_DELETE)
        {
            command_history_position = -1;
            shell_delete();
            continue;
        }

        /* ========================================================
         * TEXT
         * ======================================================== */

        if (roottext_should_insert(&event))
        {
            command_history_position = -1;
            shell_insert_codepoint(event.codepoint);
            continue;
        }
    }
}
