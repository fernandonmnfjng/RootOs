#include "types.h"
#include "multiboot.h"

#include "heap.h"
#include "rootdisplay.h"

#include "terminal.h"
#include "rootinput.h"

#include "interrupts.h"
#include "pit.h"

#include "filesystem.h"
#include "shell.h"

#include "system_config.h"
#include "rootclipboard.h"

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
     * HEAP
     * ========================================================
     */

    heap_init();


    /*
     * ========================================================
     * DISPLAY
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
     * GLOBAL CLIPBOARD
     * ========================================================
     */

    rootclipboard_init();


    /*
     * ========================================================
     * INPUT
     * ========================================================
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
     * CLOCK
     * ========================================================
     */

    pit_init(
        PIT_DEFAULT_FREQUENCY
    );


    interrupts_enable();


    /*
     * ========================================================
     * FILESYSTEM
     * ========================================================
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
     */

    interrupts_disable();


    while (1)
    {
        __asm__ volatile(
            "hlt"
        );
    }
}