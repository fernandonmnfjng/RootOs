/*
 * ============================================================
 * ROOTOS INTERRUPT STUBS
 * ============================================================
 */

.section .text


/*
 * ============================================================
 * IRQ0 - PIT TIMER
 * ============================================================
 */

.global irq0_stub

.type irq0_stub, @function


.extern pit_irq_handler


irq0_stub:

    /*
     * Guardar registros generales.
     */
    pushal


    /*
     * C espera Direction Flag limpio.
     */
    cld


    call pit_irq_handler


    /*
     * Restaurar registros.
     */
    popal


    /*
     * Volver de interrupción.
     */
    iret


.size irq0_stub, . - irq0_stub


.section .note.GNU-stack,"",@progbits