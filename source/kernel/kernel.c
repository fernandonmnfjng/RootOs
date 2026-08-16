#include "terminal.h"
#include "filesystem.h"
#include "shell.h"
#include "system_config.h"


/*
 * ============================================================
 * ROOTOS KERNEL ENTRY
 * ============================================================
 */

void kernel_main(void)
{
    /*
     * Inicializar terminal actual.
     *
     * En RootOS 0.32B esto será reemplazado
     * internamente por RootDisplay + framebuffer.
     */
    terminal_clear();


    /*
     * Inicializar filesystem virtual temporal.
     */
    filesystem_init();


    /*
     * ========================================================
     * BANNER
     * ========================================================
     */

    terminal_print(
        ROOTOS_NAME
    );

    terminal_print(
        " v"
    );

    terminal_print(
        ROOTOS_VERSION_STRING
    );

    terminal_print(
        " - "
    );

    terminal_print(
        ROOTOS_BUILD_TYPE
    );

    terminal_putchar('\n');


    terminal_print(
        ROOTOS_BANNER_SEPARATOR
    );

    terminal_putchar('\n');


    terminal_print(
        ROOTOS_BANNER_HELP
    );

    terminal_putchar('\n');

    terminal_putchar('\n');


    /*
     * Iniciar shell.
     */
    shell_run();


    /*
     * shell_run() actualmente no debería retornar.
     */
    while (1)
    {
        __asm__ volatile(
            "hlt"
        );
    }
}