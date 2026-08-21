#include "rootinput.h"

#include "ps2.h"
#include "keyboard.h"
#include "mouse.h"

#include "rootdisplay.h"
#include "memory.h"
#include "time.h"

#define ROOT_INPUT_QUEUE_SIZE 128


#define MOUSE_MASK_LEFT   0x01
#define MOUSE_MASK_RIGHT  0x02
#define MOUSE_MASK_MIDDLE 0x04


static RootInputEvent queue[
    ROOT_INPUT_QUEUE_SIZE
];


static u32 queue_head = 0;
static u32 queue_tail = 0;


static bool key_states[
    ROOT_KEY_COUNT
];


static bool mouse_ready = false;

static i32 mouse_x = 0;
static i32 mouse_y = 0;

static i32 mouse_max_x = 1023;
static i32 mouse_max_y = 767;

static u8 mouse_buttons = 0;
static bool mouse_visual_dirty = false;


static i32 press_x[4];
static i32 press_y[4];

/*
 * ============================================================
 * DOUBLE CLICK
 * ============================================================
 */

#define ROOT_DOUBLE_CLICK_MS 400

#define ROOT_DOUBLE_CLICK_DISTANCE 6


static bool last_click_valid[4];
static u64 last_click_time[4];
static i32 last_click_x[4];
static i32 last_click_y[4];

/*
 * ============================================================
 * QUEUE
 * ============================================================
 */

static void queue_push(
    const RootInputEvent* event
)
{
    /*
     * Coalesce high-frequency events before they reach the shell/editor.
     * This is especially important for wheel bursts and mouse movement:
     * one visual update should represent the newest state, not every PS/2
     * packet individually.
     */
    if (queue_head != queue_tail)
    {
        u32 previous =
            (queue_head + ROOT_INPUT_QUEUE_SIZE - 1u) %
            ROOT_INPUT_QUEUE_SIZE;

        RootInputEvent* last = &queue[previous];

        if (
            event->type == ROOT_INPUT_MOUSE_WHEEL &&
            last->type == ROOT_INPUT_MOUSE_WHEEL
        )
        {
            i32 combined = last->mouse_wheel + event->mouse_wheel;

            if (combined > 32) combined = 32;
            if (combined < -32) combined = -32;

            last->mouse_wheel = combined;
            last->mouse_x = event->mouse_x;
            last->mouse_y = event->mouse_y;
            last->mouse_buttons = event->mouse_buttons;
            last->timestamp_ms = event->timestamp_ms;
            return;
        }

        if (
            (event->type == ROOT_INPUT_MOUSE_MOVE ||
             event->type == ROOT_INPUT_MOUSE_DRAG) &&
            last->type == event->type &&
            last->mouse_buttons == event->mouse_buttons
        )
        {
            last->mouse_dx += event->mouse_dx;
            last->mouse_dy += event->mouse_dy;
            last->mouse_x = event->mouse_x;
            last->mouse_y = event->mouse_y;
            last->timestamp_ms = event->timestamp_ms;
            return;
        }
    }

    u32 next =
        (queue_head + 1u) % ROOT_INPUT_QUEUE_SIZE;

    if (next == queue_tail)
    {
        queue_tail =
            (queue_tail + 1u) % ROOT_INPUT_QUEUE_SIZE;
    }

    queue[queue_head] = *event;
    queue_head = next;
}


static bool queue_pop(
    RootInputEvent* event
)
{
    if (
        queue_tail
        ==
        queue_head
    )
    {
        return false;
    }


    *event =
        queue[
            queue_tail
        ];


    queue_tail =
        (
            queue_tail + 1
        )
        %
        ROOT_INPUT_QUEUE_SIZE;


    return true;
}


/*
 * ============================================================
 * UTIL
 * ============================================================
 */

static i32 clamp_i32(
    i32 value,
    i32 minimum,
    i32 maximum
)
{
    if (
        value < minimum
    )
    {
        return minimum;
    }


    if (
        value > maximum
    )
    {
        return maximum;
    }


    return value;
}


static i32 abs_i32(
    i32 value
)
{
    return
        value < 0
        ?
        -value
        :
        value;
}


static RootInputEvent blank_event(
    RootInputEventType type
)
{
    RootInputEvent event;


    root_memzero(
        &event,
        sizeof(event)
    );


    event.timestamp_ms =
        root_time_millis();


    event.type =
        type;


    return event;
}


/*
 * ============================================================
 * KEYBOARD
 * ============================================================
 */

static void process_keyboard(
    u8 data
)
{
    KeyboardEvent keyboard_event;


    if (
        !keyboard_feed_byte(
            data,
            &keyboard_event
        )
    )
    {
        return;
    }


    RootInputEvent event =
        blank_event(
            keyboard_event.pressed
            ?
            ROOT_INPUT_KEY_DOWN
            :
            ROOT_INPUT_KEY_UP
        );


    event.key =
        keyboard_event.key;

    event.codepoint =
        keyboard_event.codepoint;

    event.shift =
        keyboard_event.shift;

    event.ctrl =
        keyboard_event.ctrl;

    event.alt =
        keyboard_event.alt;

    event.altgr =
        keyboard_event.altgr;


    if (
        keyboard_event.key
        >
        ROOT_KEY_UNKNOWN
        &&
        keyboard_event.key
        <
        ROOT_KEY_COUNT
    )
    {
        key_states[
            keyboard_event.key
        ] =
            keyboard_event.pressed;
    }


    queue_push(
        &event
    );
}


/*
 * ============================================================
 * MOUSE
 * ============================================================
 */

static RootMouseButton mask_to_button(
    u8 mask
)
{
    if (
        mask == MOUSE_MASK_LEFT
    )
    {
        return ROOT_MOUSE_LEFT;
    }


    if (
        mask == MOUSE_MASK_RIGHT
    )
    {
        return ROOT_MOUSE_RIGHT;
    }


    if (
        mask == MOUSE_MASK_MIDDLE
    )
    {
        return ROOT_MOUSE_MIDDLE;
    }


    return ROOT_MOUSE_NONE;
}


static void emit_mouse_button(
    u8 mask,
    bool pressed
)
{
    RootMouseButton button =
        mask_to_button(
            mask
        );


    RootInputEvent event =
        blank_event(
            pressed
            ?
            ROOT_INPUT_MOUSE_BUTTON_DOWN
            :
            ROOT_INPUT_MOUSE_BUTTON_UP
        );


    event.mouse_x =
        mouse_x;

    event.mouse_y =
        mouse_y;

    event.button =
        button;

    event.mouse_buttons =
        mouse_buttons;


    queue_push(
        &event
    );


    /*
     * ========================================================
     * PRESS
     * ========================================================
     */

    if (pressed)
    {
        press_x[
            button
        ] =
            mouse_x;


        press_y[
            button
        ] =
            mouse_y;


        return;
    }


    /*
     * ========================================================
     * RELEASE -> ¿CLICK?
     * ========================================================
     */

    i32 distance_x =
        abs_i32(
            mouse_x
            -
            press_x[
                button
            ]
        );


    i32 distance_y =
        abs_i32(
            mouse_y
            -
            press_y[
                button
            ]
        );


    /*
     * Hubo demasiado movimiento:
     * fue drag.
     */
    if (
        distance_x > 4
        ||
        distance_y > 4
    )
    {
        return;
    }


    /*
     * Click normal.
     */
    RootInputEvent click =
        blank_event(
            ROOT_INPUT_MOUSE_CLICK
        );


    click.mouse_x =
        mouse_x;

    click.mouse_y =
        mouse_y;

    click.button =
        button;

    click.mouse_buttons =
        mouse_buttons;


    queue_push(
        &click
    );


    /*
     * ========================================================
     * DOUBLE CLICK
     * ========================================================
     */

    if (
        last_click_valid[
            button
        ]
    )
    {
        u64 elapsed =
            click.timestamp_ms
            -
            last_click_time[
                button
            ];


        i32 dx =
            abs_i32(
                mouse_x
                -
                last_click_x[
                    button
                ]
            );


        i32 dy =
            abs_i32(
                mouse_y
                -
                last_click_y[
                    button
                ]
            );


        if (
            elapsed
                <=
                ROOT_DOUBLE_CLICK_MS
            &&
            dx
                <=
                ROOT_DOUBLE_CLICK_DISTANCE
            &&
            dy
                <=
                ROOT_DOUBLE_CLICK_DISTANCE
        )
        {
            RootInputEvent double_click =
                blank_event(
                    ROOT_INPUT_MOUSE_DOUBLE_CLICK
                );


            double_click.mouse_x =
                mouse_x;

            double_click.mouse_y =
                mouse_y;

            double_click.button =
                button;

            double_click.mouse_buttons =
                mouse_buttons;


            queue_push(
                &double_click
            );


            /*
             * Evitar:
             *
             * click 1
             * click 2 -> double
             * click 3 -> double otra vez
             */
            last_click_valid[
                button
            ] =
                false;


            return;
        }
    }


    /*
     * Guardar como posible primer click.
     */
    last_click_valid[
        button
    ] =
        true;


    last_click_time[
        button
    ] =
        click.timestamp_ms;


    last_click_x[
        button
    ] =
        mouse_x;


    last_click_y[
        button
    ] =
        mouse_y;
}


static void process_mouse(
    u8 data
)
{
    MousePacket packet;


    if (
        !mouse_feed_byte(
            data,
            &packet
        )
    )
    {
        return;
    }


    /*
     * ========================================================
     * WHEEL
     * ========================================================
     */

    if (packet.wheel != 0)
    {
        RootInputEvent wheel =
            blank_event(
                ROOT_INPUT_MOUSE_WHEEL
            );

        wheel.mouse_x =
            mouse_x;

        wheel.mouse_y =
            mouse_y;

        wheel.mouse_wheel =
            packet.wheel;

        wheel.mouse_buttons =
            mouse_buttons;

        queue_push(
            &wheel
        );
    }


    u8 old_buttons =
        mouse_buttons;


    u8 new_buttons =
        0;


    if (
        packet.left
    )
    {
        new_buttons |=
            MOUSE_MASK_LEFT;
    }


    if (
        packet.right
    )
    {
        new_buttons |=
            MOUSE_MASK_RIGHT;
    }


    if (
        packet.middle
    )
    {
        new_buttons |=
            MOUSE_MASK_MIDDLE;
    }


    /*
     * ========================================================
     * MOVEMENT
     * ========================================================
     */

    if (
        packet.dx != 0
        ||
        packet.dy != 0
    )
    {
        mouse_x =
            clamp_i32(
                mouse_x
                +
                packet.dx,

                0,

                mouse_max_x
            );


        mouse_y =
            clamp_i32(
                mouse_y
                +
                packet.dy,

                0,

                mouse_max_y
            );


        /*
         * ====================================================
         * MOVE GRAPHICAL CURSOR
         * ====================================================
         *
         * Esto ocurre directamente en RootInput.
         *
         * No depende de la shell ni de RootEdit.
         */

        /* Defer the software cursor redraw until the PS/2 FIFO is drained. */
        mouse_visual_dirty = true;


        RootInputEvent move =
            blank_event(
                new_buttons
                ?
                ROOT_INPUT_MOUSE_DRAG
                :
                ROOT_INPUT_MOUSE_MOVE
            );


        move.mouse_x =
            mouse_x;


        move.mouse_y =
            mouse_y;


        move.mouse_dx =
            packet.dx;


        move.mouse_dy =
            packet.dy;


        move.mouse_buttons =
            new_buttons;


        queue_push(
            &move
        );
    }


    /*
     * ========================================================
     * BUTTON STATE
     * ========================================================
     */

    mouse_buttons =
        new_buttons;


    u8 changed =
        old_buttons
        ^
        new_buttons;


    const u8 masks[3] =
    {
        MOUSE_MASK_LEFT,

        MOUSE_MASK_RIGHT,

        MOUSE_MASK_MIDDLE
    };


    for (
        u32 i = 0;
        i < 3;
        i++
    )
    {
        u8 mask =
            masks[i];


        if (
            changed
            &
            mask
        )
        {
            emit_mouse_button(
                mask,

                (
                    new_buttons
                    &
                    mask
                )
                !=
                0
            );
        }
    }
}


/*
 * ============================================================
 * INIT
 * ============================================================
 */

void rootinput_init(void)
{
    /*
     * ========================================================
     * EVENT QUEUE
     * ========================================================
     */

    queue_head =
        0;


    queue_tail =
        0;


    /*
     * ========================================================
     * KEY STATES
     * ========================================================
     */

    root_memzero(
        key_states,
        sizeof(key_states)
    );


    /*
     * ========================================================
     * MOUSE PRESS
     * ========================================================
     */

    root_memzero(
        press_x,
        sizeof(press_x)
    );


    root_memzero(
        press_y,
        sizeof(press_y)
    );


    /*
     * ========================================================
     * DOUBLE CLICK
     * ========================================================
     */

    root_memzero(
        last_click_valid,
        sizeof(last_click_valid)
    );


    root_memzero(
        last_click_time,
        sizeof(last_click_time)
    );


    root_memzero(
        last_click_x,
        sizeof(last_click_x)
    );


    root_memzero(
        last_click_y,
        sizeof(last_click_y)
    );


    /*
     * ========================================================
     * KEYBOARD
     * ========================================================
     */

    keyboard_reset();


    /*
     * ========================================================
     * PS/2
     * ========================================================
     */

    ps2_flush();


    /*
     * ========================================================
     * MOUSE SCREEN LIMITS
     * ========================================================
     */

    if (
        rootdisplay_ready()
    )
    {
        mouse_max_x =
            (i32)rootdisplay_width()
            -
            1;


        mouse_max_y =
            (i32)rootdisplay_height()
            -
            1;
    }

    else
    {
        mouse_max_x =
            1023;


        mouse_max_y =
            767;
    }


    /*
     * ========================================================
     * INITIAL POSITION
     * ========================================================
     */

    mouse_x =
        mouse_max_x
        /
        2;


    mouse_y =
        mouse_max_y
        /
        2;


    mouse_buttons =
        0;

    mouse_visual_dirty =
        false;


    /*
     * ========================================================
     * DISABLE OLD CURSOR STATE
     * ========================================================
     */

    if (
        rootdisplay_ready()
    )
    {
        rootdisplay_cursor_enable(
            false
        );
    }


    /*
     * ========================================================
     * INITIALIZE PS/2 MOUSE
     * ========================================================
     */

    mouse_ready =
        mouse_init();


    /*
     * ========================================================
     * SHOW ROOTOS CURSOR
     * ========================================================
     */

    if (
        mouse_ready
        &&
        rootdisplay_ready()
    )
    {
        /*
         * Primero colocarlo.
         */

        rootdisplay_cursor_move(
            mouse_x,
            mouse_y
        );


        /*
         * Después hacerlo visible.
         */

        rootdisplay_cursor_enable(
            true
        );
    }
}

/*
 * ============================================================
 * POLL CONTROLLER
 * ============================================================
 */

void rootinput_poll(void)
{
    while (
        ps2_has_data()
    )
    {
        u8 status =
            ps2_status();


        u8 data =
            ps2_read_data();


        /*
         * Bit 5:
         *
         * 0 = keyboard
         * 1 = mouse
         */
        if (
            status
            &
            PS2_STATUS_MOUSE_DATA
        )
        {
            if (
                mouse_ready
            )
            {
                process_mouse(
                    data
                );
            }
        }

        else
        {
            process_keyboard(
                data
            );
        }
    }

    if (
        mouse_visual_dirty &&
        rootdisplay_ready()
    )
    {
        rootdisplay_cursor_move(
            mouse_x,
            mouse_y
        );

        mouse_visual_dirty =
            false;
    }
}


/*
 * ============================================================
 * NEXT EVENT
 * ============================================================
 */

bool rootinput_next_event(
    RootInputEvent* event
)
{
    if (
        event == NULL
    )
    {
        return false;
    }


    /*
     * Procesar cualquier byte PS/2
     * pendiente.
     */
    rootinput_poll();


    /*
     * Extraer evento más antiguo.
     */
    return queue_pop(
        event
    );
}


/*
 * ============================================================
 * WAIT EVENT
 * ============================================================
 */

RootInputEvent rootinput_wait_event(void)
{
    RootInputEvent event;


    while (1)
    {
        /*
         * ¿Hay evento disponible?
         */
        if (
            rootinput_next_event(
                &event
            )
        )
        {
            return event;
        }


        /*
         * Dormir CPU hasta la próxima
         * interrupción.
         *
         * PIT despierta periódicamente
         * la CPU.
         */
        __asm__ volatile(
            "hlt"
        );
    }
}


/*
 * ============================================================
 * KEY STATE
 * ============================================================
 */

bool rootinput_key_down(
    RootKey key
)
{
    if (
        key <= ROOT_KEY_UNKNOWN
        ||
        key >= ROOT_KEY_COUNT
    )
    {
        return false;
    }


    /*
     * Actualizar datos PS/2 pendientes.
     */
    rootinput_poll();


    return key_states[
        key
    ];
}


/*
 * ============================================================
 * MOUSE X
 * ============================================================
 */

i32 rootinput_mouse_x(void)
{
    rootinput_poll();


    return mouse_x;
}


/*
 * ============================================================
 * MOUSE Y
 * ============================================================
 */

i32 rootinput_mouse_y(void)
{
    rootinput_poll();


    return mouse_y;
}


/*
 * ============================================================
 * MOUSE AVAILABLE
 * ============================================================
 */

bool rootinput_mouse_available(void)
{
    return mouse_ready;
}