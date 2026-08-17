#include "interrupts.h"

#include "memory.h"

#include "pic.h"


#define IDT_ENTRY_COUNT 256


typedef struct __attribute__((packed))
{
    u16 offset_low;

    u16 selector;

    u8 zero;

    u8 flags;

    u16 offset_high;

} IdtEntry;


typedef struct __attribute__((packed))
{
    u16 limit;

    u32 base;

} IdtDescriptor;


static IdtEntry idt[
    IDT_ENTRY_COUNT
];


static IdtDescriptor idtr;


/*
 * Implementado en interrupt_stubs.S
 */
extern void irq0_stub(void);


/*
 * ============================================================
 * CODE SEGMENT
 * ============================================================
 *
 * No asumimos que CS sea 0x08.
 *
 * Leemos el selector que GRUB nos dejó activo.
 */

static u16 interrupts_read_cs(void)
{
    u16 selector;


    __asm__ volatile(
        "mov %%cs, %0"
        :
        "=r"(selector)
    );


    return selector;
}


/*
 * ============================================================
 * IDT ENTRY
 * ============================================================
 */

static void idt_set_gate(
    u8 vector,
    void (*handler)(void)
)
{
    u32 address =
        (u32)(
            usize
        )
        handler;


    idt[
        vector
    ].offset_low =
        (
            u16
        )(
            address
            &
            0xFFFF
        );


    idt[
        vector
    ].selector =
        interrupts_read_cs();


    idt[
        vector
    ].zero =
        0;


    /*
     * 0x8E:
     *
     * Present
     * Ring 0
     * 32-bit interrupt gate
     */
    idt[
        vector
    ].flags =
        0x8E;


    idt[
        vector
    ].offset_high =
        (
            u16
        )(
            (
                address
                >>
                16
            )
            &
            0xFFFF
        );
}


/*
 * ============================================================
 * INIT
 * ============================================================
 */

void interrupts_init(void)
{
    interrupts_disable();


    root_memzero(
        idt,
        sizeof(idt)
    );


    /*
     * Hardware IRQs:
     *
     * IRQ0 → 32
     * IRQ1 → 33
     * ...
     * IRQ15 → 47
     */
    pic_remap(
        0x20,
        0x28
    );


    /*
     * Empezar con todo bloqueado.
     *
     * Cada driver habilitará solamente
     * lo que necesite.
     */
    pic_mask_all();


    /*
     * PIT:
     *
     * IRQ0 → vector 32.
     */
    idt_set_gate(
        32,
        irq0_stub
    );


    idtr.limit =
        sizeof(idt)
        -
        1;


    idtr.base =
        (u32)(
            usize
        )
        idt;


    __asm__ volatile(
        "lidt %0"
        :
        : "m"(idtr)
    );
}


void interrupts_enable(void)
{
    __asm__ volatile(
        "sti"
    );
}


void interrupts_disable(void)
{
    __asm__ volatile(
        "cli"
    );
}


bool interrupts_enabled(void)
{
    u32 flags;


    __asm__ volatile(
        "pushf\n"
        "pop %0"
        :
        "=r"(flags)
    );


    return
        (
            flags
            &
            (
                1u
                <<
                9
            )
        )
        !=
        0;
}