/*
 * RootOS i386 linker script.
 *
 * Multiboot v1 requires the complete header to be 32-bit aligned and fully
 * contained in the first 8192 bytes of the OS image.  Put .multiboot first
 * inside the first loadable text section so its file offset is deterministic.
 */
ENTRY(_start)
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)

SECTIONS
{
    /* Conventional physical load address for the 32-bit kernel. */
    . = 1M;

    /*
     * Keep the Multiboot header FIRST.  Combining it with .text avoids an
     * orphan/non-loadable section being placed after other ELF contents.
     */
    .text ALIGN(4K) :
    {
        KEEP(*(.multiboot))
        *(.text .text.*)
    }

    .rodata ALIGN(4K) :
    {
        *(.rodata .rodata.*)
    }

    .data ALIGN(4K) :
    {
        *(.data .data.*)
    }

    .bss ALIGN(4K) (NOLOAD) :
    {
        *(COMMON)
        *(.bss .bss.*)
    }

    /* These are not useful to the freestanding kernel and can move the
       Multiboot header to a later file offset on some toolchains. */
    /DISCARD/ :
    {
        *(.note.gnu.build-id)
        *(.note.GNU-stack)
        *(.comment)
        *(.eh_frame*)
    }
}
