#include "shell.h"
#include "terminal.h"
#include "keyboard.h"
#include "filesystem.h"
#include "io.h"
#include "string.h"
#include "system_config.h"

#define COMMAND_BUFFER_SIZE 128

static char command_buffer[COMMAND_BUFFER_SIZE];

static u32 command_length = 0;

/* Posicion del cursor dentro del comando */
static u32 command_cursor = 0;

/* Cantidad de caracteres dibujados */
static u32 rendered_length = 0;

/* Posicion de pantalla donde empieza el texto del usuario */
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
     * ENTER vacío.
     */
    if (
        command[0] == '\0'
    )
    {
        return;
    }


    /*
     * help
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
     * clear
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
     * about
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
     * echo
     */
    if (
        root_streq(
            command,
            "echo"
        )
    )
    {
        terminal_putchar('\n');

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
     * =================================
     * godir
     * =================================
     */

    if (
        root_streq(
            command,
            "godir"
        )
    )
    {
        terminal_print(
            "Uso: godir <ruta>\n"
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
     * =================================
     * seedir("nombre")
     * =================================
     *
     * IMPORTANTE:
     * esto debe comprobarse antes
     * del seedir normal.
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
     * =================================
     * seedir
     * =================================
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
     * =================================
     * see
     * =================================
     */

    if (
        root_streq(
            command,
            "see"
        )
    )
    {
        command_see("");

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
     * reboot
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
     * shutdown
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
     * Desconocido.
     */
    terminal_print(
        "Comando desconocido: "
    );

    terminal_print(command);

    terminal_putchar('\n');
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

static void shell_update_cursor(void)
{
    terminal_set_cursor(
        input_start_col + command_cursor,
        input_start_row
    );
}

/*
 * Volver a dibujar la línea completa.
 *
 * Esto permite insertar texto
 * en medio del comando.
 */
static void shell_redraw_line(void)
{
    terminal_set_cursor(
        input_start_col,
        input_start_row
    );


    /*
     * Imprimir contenido actual.
     */
    for (
        u32 i = 0;
        i < command_length;
        i++
    )
    {
        terminal_putchar(
            command_buffer[i]
        );
    }


    /*
     * Borrar caracteres sobrantes
     * de una versión anterior.
     */
    for (
        u32 i = command_length;
        i < rendered_length;
        i++
    )
    {
        terminal_putchar(' ');
    }


    rendered_length =
        command_length;


    shell_update_cursor();
}


/*
 * Insertar un carácter exactamente
 * donde está el cursor.
 */
static void shell_insert_character(
    char c
)
{
    /*
     * Dejamos un espacio para \0.
     */
    if (
        command_length
        >=
        COMMAND_BUFFER_SIZE - 1
    )
    {
        return;
    }


    /*
     * Esta primera versión del editor
     * mantiene la entrada en una línea.
     */
    if (
        input_start_col
        +
        command_length
        >=
        79
    )
    {
        return;
    }


    /*
     * Mover todo una posición
     * hacia la derecha.
     */
    for (
        u32 i = command_length;
        i > command_cursor;
        i--
    )
    {
        command_buffer[i] =
            command_buffer[i - 1];
    }


    command_buffer[
        command_cursor
    ] = c;


    command_cursor++;

    command_length++;


    command_buffer[
        command_length
    ] = '\0';


    shell_redraw_line();
}


/*
 * Backspace.
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

        i < command_length - 1;

        i++
    )
    {
        command_buffer[i] =
            command_buffer[i + 1];
    }


    command_cursor--;

    command_length--;


    command_buffer[
        command_length
    ] = '\0';


    shell_redraw_line();
}


/*
 * Delete.
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
        u32 i = command_cursor;

        i < command_length - 1;

        i++
    )
    {
        command_buffer[i] =
            command_buffer[i + 1];
    }


    command_length--;


    command_buffer[
        command_length
    ] = '\0';


    shell_redraw_line();
}


/*
 * Borrar comando actual completo.
 */
static void shell_clear_input(void)
{
    command_length = 0;

    command_cursor = 0;

    command_buffer[0] =
        '\0';


    shell_redraw_line();
}

void shell_run(void)
{
    command_length = 0;

    command_cursor = 0;

    rendered_length = 0;

    command_buffer[0] =
        '\0';

    shell_prompt();


    while (1)
    {
        KeyEvent event =
            keyboard_read_event();


        /*
         * =================================
         * CTRL
         * =================================
         */

        if (
            event.type == KEY_CHARACTER
            &&
            event.ctrl
        )
        {
            /*
             * CTRL + A
             *
             * Inicio de linea.
             */
            if (
                event.character == 'a'
                ||
                event.character == 'A'
            )
            {
                command_cursor = 0;

                shell_update_cursor();

                continue;
            }


            /*
             * CTRL + E
             *
             * Final de linea.
             */
            if (
                event.character == 'e'
                ||
                event.character == 'E'
            )
            {
                command_cursor =
                    command_length;

                shell_update_cursor();

                continue;
            }


            /*
             * CTRL + U
             *
             * Borrar linea.
             */
            if (
                event.character == 'u'
                ||
                event.character == 'U'
            )
            {
                shell_clear_input();

                continue;
            }


            /*
             * CTRL + L
             *
             * Limpiar pantalla.
             */
            if (
                event.character == 'l'
                ||
                event.character == 'L'
            )
            {
                terminal_clear();

                shell_prompt();

                /*
                 * Volver a mostrar lo
                 * que estaba escrito.
                 */
                shell_redraw_line();

                continue;
            }


            /*
             * CTRL + C
             *
             * Cancelar linea actual.
             */
            if (
                event.character == 'c'
                ||
                event.character == 'C'
            )
            {
                terminal_print("^C\n");

                command_length = 0;

                command_cursor = 0;

                rendered_length = 0;

                command_buffer[0] =
                    '\0';

                shell_prompt();

                continue;
            }
        }


        /*
         * =================================
         * ENTER
         * =================================
         */

        if (
            event.type == KEY_ENTER
        )
        {
            /*
             * Cursor al final antes
             * de cambiar de linea.
             */
            command_cursor =
                command_length;

            shell_update_cursor();

            terminal_putchar('\n');


            /*
             * Final del string.
             */
            command_buffer[
                command_length
            ] = '\0';


            /*
             * Ejecutar comando.
             *
             * Esta funcion ya la tenias.
             */
            execute_command(
                command_buffer
            );


            /*
             * Limpiar entrada.
             */
            command_length = 0;

            command_cursor = 0;

            rendered_length = 0;

            command_buffer[0] =
                '\0';


            /*
             * Nuevo prompt.
             */
            shell_prompt();

            continue;
        }


        /*
         * =================================
         * FLECHA IZQUIERDA
         * =================================
         */

        if (
            event.type == KEY_LEFT
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


        /*
         * =================================
         * FLECHA DERECHA
         * =================================
         */

        if (
            event.type == KEY_RIGHT
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


        /*
         * =================================
         * HOME
         * =================================
         */

        if (
            event.type == KEY_HOME
        )
        {
            command_cursor = 0;

            shell_update_cursor();

            continue;
        }


        /*
         * =================================
         * END
         * =================================
         */

        if (
            event.type == KEY_END
        )
        {
            command_cursor =
                command_length;

            shell_update_cursor();

            continue;
        }


        /*
         * =================================
         * BACKSPACE
         * =================================
         */

        if (
            event.type
            ==
            KEY_BACKSPACE
        )
        {
            shell_backspace();

            continue;
        }


        /*
         * =================================
         * DELETE
         * =================================
         */

        if (
            event.type
            ==
            KEY_DELETE
        )
        {
            shell_delete();

            continue;
        }


        /*
         * =================================
         * CARACTER NORMAL
         * =================================
         */

        if (
            event.type
            ==
            KEY_CHARACTER
        )
        {
            shell_insert_character(
                event.character
            );

            continue;
        }
    }
}
