/*
 * RootOS x86 Multiboot v1 entry.
 *
 * Real-hardware rules followed here:
 *   - advertise a linear framebuffer to GRUB;
 *   - preserve Multiboot EAX/EBX before touching AX;
 *   - install our own GDT before reloading any segment register;
 *   - clear DF because Multiboot leaves most EFLAGS bits undefined;
 *   - create and align our own stack before entering C.
 */

.set MB_MAGIC,    0x1BADB002
.set MB_ALIGN,    (1 << 0)
.set MB_MEMINFO,  (1 << 1)
.set MB_VIDEO,    (1 << 2)
.set MB_FLAGS,    (MB_ALIGN | MB_MEMINFO | MB_VIDEO)
.set MB_CHECKSUM, -(MB_MAGIC + MB_FLAGS)

.set GDT_CODE_SEL, 0x08
.set GDT_DATA_SEL, 0x10

.section .multiboot,"a",@progbits
.align 4
.long MB_MAGIC
.long MB_FLAGS
.long MB_CHECKSUM

/* Multiboot v1 fixed address slots (unused because bit 16 is clear). */
.long 0                  /* header_addr */
.long 0                  /* load_addr */
.long 0                  /* load_end_addr */
.long 0                  /* bss_end_addr */
.long 0                  /* entry_addr */

/* Graphics preference. Zero dimensions mean: any linear mode is acceptable. */
.long 0                  /* mode_type = linear graphics */
.long 0                  /* width = no preference */
.long 0                  /* height = no preference */
.long 0                  /* depth = no preference */

/*
 * Known flat 32-bit GDT.
 * Multiboot guarantees usable segment registers at entry, but the exact
 * selectors are undefined and GDTR itself may be invalid.  RootOS owns the
 * descriptor table before C/interrupt code can make assumptions about 0x08
 * and 0x10.
 */
.section .rodata
.align 8
gdt_start:
    .quad 0x0000000000000000      /* null */
    .quad 0x00CF9A000000FFFF      /* 0x08: base=0 limit=4GiB code */
    .quad 0x00CF92000000FFFF      /* 0x10: base=0 limit=4GiB data */
gdt_end:

gdt_descriptor:
    .word gdt_end - gdt_start - 1
    .long gdt_start

.section .bss
.align 16
stack_bottom:
.skip 32768
stack_top:

.section .text
.global _start
.type _start, @function
.extern kernel_main

_start:
    cli

    /* Preserve Multiboot arguments before AX is used for segment selectors. */
    mov %eax, %esi
    mov %ebx, %edi

    /* The Direction Flag is undefined on Multiboot entry. */
    cld

    /* Establish descriptor state that RootOS itself controls. */
    lgdt gdt_descriptor
    ljmp $GDT_CODE_SEL, $gdt_ready

gdt_ready:
    mov $GDT_DATA_SEL, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    /* Own stack, 16-byte aligned. */
    mov $stack_top, %esp
    and $-16, %esp
    xor %ebp, %ebp

    /*
     * i386 SysV: keep ESP 16-byte aligned immediately before CALL.
     * cdecl: kernel_main(magic, info) => push info, then magic.
     */
    sub $8, %esp
    push %edi
    push %esi
    call kernel_main

1:
    cli
    hlt
    jmp 1b

.size _start, . - _start
.section .note.GNU-stack,"",@progbits
