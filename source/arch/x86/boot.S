/*
 * ============================================================
 * ROOTOS MULTIBOOT HEADER
 * ============================================================
 */

.set ALIGN,    1<<0
.set MEMINFO,  1<<1
.set VIDEO,    1<<2

.set FLAGS,    ALIGN | MEMINFO | VIDEO

.set MAGIC,    0x1BADB002
.set CHECKSUM, -(MAGIC + FLAGS)


.section .multiboot,"a"

.align 4

.long MAGIC
.long FLAGS
.long CHECKSUM


/*
 * ============================================================
 * GRAPHICS REQUEST
 * ============================================================
 *
 * mode_type = 0
 *     framebuffer lineal
 *
 * width  = 1024
 * height = 768
 * depth  = 32 bits
 *
 * Es una preferencia.
 * GRUB puede entregar otro modo compatible.
 */

.long 0
.long 1024
.long 768
.long 32


/*
 * ============================================================
 * STACK
 * ============================================================
 */

.section .bss

.align 16

stack_bottom:

.skip 16384

stack_top:


/*
 * ============================================================
 * ENTRY
 * ============================================================
 */

.section .text

.global _start

.type _start, @function


.extern kernel_main


_start:

    /*
     * Crear stack.
     */
    mov $stack_top, %esp


    /*
     * Al entrar mediante Multiboot:
     *
     * EAX = magic 0x2BADB002
     * EBX = dirección de MultibootInfo
     *
     * kernel_main(
     *     magic,
     *     multiboot_info
     * )
     *
     * cdecl coloca argumentos
     * de derecha a izquierda.
     */

    push %ebx
    push %eax

    call kernel_main


    /*
     * Si retorna:
     */
    add $8, %esp


hang:

    cli
    hlt

    jmp hang


.size _start, . - _start


.section .note.GNU-stack,"",@progbits