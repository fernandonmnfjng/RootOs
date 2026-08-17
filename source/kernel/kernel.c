#include "types.h"

#include "multiboot.h"

#include "rootdisplay.h"

#include "terminal.h"

#include "rootinput.h"

#include "interrupts.h"

#include "pit.h"

#include "filesystem.h"

#include "shell.h"

#include "system_config.h"


/*
 * ============================================================
 * ROOTOS KERNEL ENTRY
 * ============================================================
 */

void kernel_main(
    u32 multiboot_magic,
    u32 multiboot_info_address
)
{
    /*
     * ========================================================
     * DISPLAY / FRAMEBUFFER
     * ========================================================
     */

    if (
        multiboot_magic
        ==
        MULTIBOOT_BOOTLOADER_MAGIC
    )
    {
        const MultibootInfo* multiboot =
            (
                const MultibootInfo*
            )(
                usize
            )
            multiboot_info_address;


        rootdisplay_init(
            multiboot
        );
    }


    /*
     * ========================================================
     * TERMINAL
     * ========================================================
     */

    terminal_init();

    terminal_clear();


    /*
     * ========================================================
     * INPUT
     * ========================================================
     *
     * PS/2 keyboard + mouse.
     *
     * Todavía se inicializa antes de activar IRQ.
     */

    rootinput_init();


    /*
     * ========================================================
     * INTERRUPTS
     * ========================================================
     */

    interrupts_init();


    /*
     * ========================================================
     * PIT / ROOT CLOCK
     * ========================================================
     *
     * 1000 Hz:
     *
     * aproximadamente 1 tick = 1 ms.
     */

    pit_init(
        PIT_DEFAULT_FREQUENCY
    );


    /*
     * Activar interrupciones después
     * de que IDT/PIC/PIT estén listos.
     */

    interrupts_enable();


    /*
     * ========================================================
     * FILESYSTEM
     * ========================================================
     */

    filesystem_init();


    /*
     * ========================================================
     * ROOTOS BANNER
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

    terminal_putchar(
        '\n'
    );


    terminal_print(
        ROOTOS_BANNER_SEPARATOR
    );

    terminal_putchar(
        '\n'
    );


    terminal_print(
        ROOTOS_BANNER_HELP
    );

    terminal_putchar(
        '\n'
    );

    terminal_putchar(
        '\n'
    );


    /*
     * ========================================================
     * SHELL
     * ========================================================
     */

    shell_run();


    /*
     * ========================================================
     * FALLBACK
     * ========================================================
     *
     * shell_run() no debería retornar.
     */

    interrupts_disable();


    while (1)
    {
        __asm__ volatile(
            "hlt"
        );
    }
}