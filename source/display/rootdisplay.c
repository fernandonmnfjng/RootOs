/*
 * RootOS v0.47.6-1 UEFI/GOP framebuffer hotfix.
 *
 * The installer preserves the original RootOS rootdisplay.c as:
 *
 *     rootdisplay_legacy_impl.c
 *
 * We compile that implementation in this translation unit, renaming only its
 * original rootdisplay_init().  All existing drawing, font, cursor, scrolling,
 * selection and performance code therefore stays unchanged.
 *
 * The replacement rootdisplay_init() below adds the missing case for UEFI GOP
 * framebuffers whose physical address/range is above 4 GiB.  RootOS remains an
 * i386 kernel; a minimal PAE mapping exposes that physical range through a
 * fixed 512 MiB virtual framebuffer window.
 */

#define rootdisplay_init rootdisplay_legacy_init
#include "rootdisplay_legacy_impl.c"
#undef rootdisplay_init

#include "io.h"

/* ============================================================
 * EARLY SERIAL DIAGNOSTICS
 * ============================================================ */

#define ROOTOS_COM1_BASE 0x3F8u
#define ROOTOS_SERIAL_SPIN_LIMIT 100000u

static bool uefi_debug_serial_initialized = false;

static void uefi_debug_serial_init(void)
{
    if (uefi_debug_serial_initialized)
        return;

    outb((u16)(ROOTOS_COM1_BASE + 1u), 0x00u);
    outb((u16)(ROOTOS_COM1_BASE + 3u), 0x80u);
    outb((u16)(ROOTOS_COM1_BASE + 0u), 0x01u); /* 115200 baud */
    outb((u16)(ROOTOS_COM1_BASE + 1u), 0x00u);
    outb((u16)(ROOTOS_COM1_BASE + 3u), 0x03u); /* 8N1 */
    outb((u16)(ROOTOS_COM1_BASE + 2u), 0xC7u);
    outb((u16)(ROOTOS_COM1_BASE + 4u), 0x0Bu);

    uefi_debug_serial_initialized = true;
}

static void uefi_debug_putc(char value)
{
    uefi_debug_serial_init();

    if (value == '\n')
        uefi_debug_putc('\r');

    for (u32 spin = 0; spin < ROOTOS_SERIAL_SPIN_LIMIT; spin++)
    {
        if ((inb((u16)(ROOTOS_COM1_BASE + 5u)) & 0x20u) != 0u)
            break;
    }

    outb(ROOTOS_COM1_BASE, (u8)value);
}

static void uefi_debug_write(const char* text)
{
    if (text == NULL)
        return;

    while (*text != '\0')
        uefi_debug_putc(*text++);
}

static void uefi_debug_line(const char* text)
{
    uefi_debug_write(text);
    uefi_debug_putc('\n');
}

static char uefi_hex_digit(u8 value)
{
    value &= 0x0Fu;

    return value < 10u
        ? (char)('0' + value)
        : (char)('A' + value - 10u);
}

static void uefi_debug_hex64(u64 value)
{
    uefi_debug_write("0x");

    for (i32 shift = 60; shift >= 0; shift -= 4)
        uefi_debug_putc(uefi_hex_digit((u8)(value >> (u32)shift)));
}

static void uefi_debug_u32(u32 value)
{
    char buffer[10];
    u32 used = 0u;

    if (value == 0u)
    {
        uefi_debug_putc('0');
        return;
    }

    while (value != 0u && used < sizeof(buffer))
    {
        buffer[used++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (used != 0u)
        uefi_debug_putc(buffer[--used]);
}

/* ============================================================
 * PAE FRAMEBUFFER WINDOW
 * ============================================================ */

#define ROOTOS_FB_WINDOW_BASE 0x80000000u
#define ROOTOS_FB_WINDOW_SIZE 0x20000000u /* 512 MiB */

#define ROOTOS_PAE_PDPT_COUNT 4u
#define ROOTOS_PAE_PDE_COUNT  512u

#define ROOTOS_PAGE_SHIFT_2M 21u
#define ROOTOS_PAGE_SIZE_2M  (1ULL << ROOTOS_PAGE_SHIFT_2M)
#define ROOTOS_PAGE_MASK_2M  (ROOTOS_PAGE_SIZE_2M - 1ULL)

#define ROOTOS_PAE_PRESENT (1ULL << 0)
#define ROOTOS_PAE_RW      (1ULL << 1)
#define ROOTOS_PAE_PWT     (1ULL << 3)
#define ROOTOS_PAE_PCD     (1ULL << 4)
#define ROOTOS_PAE_PS      (1ULL << 7)

#define ROOTOS_PAE_PDE_ADDRESS_MASK  0x000FFFFFFFE00000ULL
#define ROOTOS_PAE_PDPT_ADDRESS_MASK 0x000FFFFFFFFFF000ULL

#define ROOTOS_CR0_PG  (1u << 31)
#define ROOTOS_CR4_PSE (1u << 4)
#define ROOTOS_CR4_PAE (1u << 5)

static u64 uefi_pdpt[ROOTOS_PAE_PDPT_COUNT]
    __attribute__((aligned(4096)));

static u64 uefi_page_directories[
    ROOTOS_PAE_PDPT_COUNT
][
    ROOTOS_PAE_PDE_COUNT
]
    __attribute__((aligned(4096)));

static bool uefi_pae_active = false;

static void uefi_cpuid(
    u32 leaf,
    u32* eax,
    u32* ebx,
    u32* ecx,
    u32* edx
)
{
    u32 a;
    u32 b;
    u32 c;
    u32 d;

    __asm__ volatile(
        "cpuid"
        : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
        : "a"(leaf), "c"(0u)
    );

    if (eax != NULL) *eax = a;
    if (ebx != NULL) *ebx = b;
    if (ecx != NULL) *ecx = c;
    if (edx != NULL) *edx = d;
}

static bool uefi_cpu_supports_pae(void)
{
    u32 maximum_leaf = 0u;
    u32 features_edx = 0u;

    uefi_cpuid(
        0u,
        &maximum_leaf,
        NULL,
        NULL,
        NULL
    );

    if (maximum_leaf < 1u)
        return false;

    uefi_cpuid(
        1u,
        NULL,
        NULL,
        NULL,
        &features_edx
    );

    /* CPUID.01H:EDX.PSE[3], PAE[6]. */
    const u32 required =
        (1u << 3) |
        (1u << 6);

    return (features_edx & required) == required;
}

static u32 uefi_read_cr0(void)
{
    u32 value;

    __asm__ volatile(
        "mov %%cr0, %0"
        : "=r"(value)
    );

    return value;
}

static u32 uefi_read_cr4(void)
{
    u32 value;

    __asm__ volatile(
        "mov %%cr4, %0"
        : "=r"(value)
    );

    return value;
}

static void uefi_write_cr0(u32 value)
{
    __asm__ volatile(
        "mov %0, %%cr0"
        :
        : "r"(value)
        : "memory"
    );
}

static void uefi_write_cr3(u32 value)
{
    __asm__ volatile(
        "mov %0, %%cr3"
        :
        : "r"(value)
        : "memory"
    );
}

static void uefi_write_cr4(u32 value)
{
    __asm__ volatile(
        "mov %0, %%cr4"
        :
        : "r"(value)
        : "memory"
    );
}

static void uefi_build_identity_map(void)
{
    for (
        u32 pdpt_index = 0u;
        pdpt_index < ROOTOS_PAE_PDPT_COUNT;
        pdpt_index++
    )
    {
        u64 directory_physical =
            (u64)(u32)(usize)&uefi_page_directories[pdpt_index][0];

        uefi_pdpt[pdpt_index] =
            (directory_physical & ROOTOS_PAE_PDPT_ADDRESS_MASK) |
            ROOTOS_PAE_PRESENT;

        for (
            u32 pde_index = 0u;
            pde_index < ROOTOS_PAE_PDE_COUNT;
            pde_index++
        )
        {
            u64 page_number =
                (u64)pdpt_index *
                (u64)ROOTOS_PAE_PDE_COUNT +
                (u64)pde_index;

            u64 physical =
                page_number <<
                ROOTOS_PAGE_SHIFT_2M;

            uefi_page_directories[pdpt_index][pde_index] =
                (physical & ROOTOS_PAE_PDE_ADDRESS_MASK) |
                ROOTOS_PAE_PRESENT |
                ROOTOS_PAE_RW |
                ROOTOS_PAE_PS;
        }
    }
}

static bool uefi_install_framebuffer_window(
    u64 physical_address,
    usize byte_length,
    volatile u8** mapped_address
)
{
    if (
        mapped_address == NULL ||
        byte_length == 0u
    )
    {
        return false;
    }

    u64 page_offset =
        physical_address &
        ROOTOS_PAGE_MASK_2M;

    u64 aligned_physical =
        physical_address -
        page_offset;

    u64 span =
        page_offset +
        (u64)byte_length;

    if (span < (u64)byte_length)
        return false;

    u64 page_count64 =
        (span + ROOTOS_PAGE_MASK_2M) >>
        ROOTOS_PAGE_SHIFT_2M;

    u32 maximum_pages =
        ROOTOS_FB_WINDOW_SIZE >>
        ROOTOS_PAGE_SHIFT_2M;

    if (
        page_count64 == 0u ||
        page_count64 > (u64)maximum_pages
    )
    {
        return false;
    }

    u64 last_page =
        aligned_physical +
        ((page_count64 - 1u) <<
        ROOTOS_PAGE_SHIFT_2M);

    if (
        (aligned_physical &
            ~ROOTOS_PAE_PDE_ADDRESS_MASK) != 0u ||
        (last_page &
            ~ROOTOS_PAE_PDE_ADDRESS_MASK) != 0u
    )
    {
        return false;
    }

    const u32 pdpt_index =
        ROOTOS_FB_WINDOW_BASE >>
        30;

    const u32 first_pde =
        (
            ROOTOS_FB_WINDOW_BASE >>
            ROOTOS_PAGE_SHIFT_2M
        ) &
        0x1FFu;

    const u32 page_count =
        (u32)page_count64;

    if (
        first_pde +
        page_count >
        ROOTOS_PAE_PDE_COUNT
    )
    {
        return false;
    }

    for (u32 index = 0u; index < page_count; index++)
    {
        u64 physical =
            aligned_physical +
            ((u64)index <<
            ROOTOS_PAGE_SHIFT_2M);

        /*
         * Conservative uncached framebuffer mapping.  RootOS can later add
         * PAT/write-combining as a separate performance change.
         */
        uefi_page_directories[
            pdpt_index
        ][
            first_pde + index
        ] =
            (physical & ROOTOS_PAE_PDE_ADDRESS_MASK) |
            ROOTOS_PAE_PRESENT |
            ROOTOS_PAE_RW |
            ROOTOS_PAE_PWT |
            ROOTOS_PAE_PCD |
            ROOTOS_PAE_PS;
    }

    *mapped_address =
        (volatile u8*)(usize)(
            ROOTOS_FB_WINDOW_BASE +
            (u32)page_offset
        );

    return true;
}

static bool uefi_map_high_framebuffer(
    u64 physical_address,
    usize byte_length,
    volatile u8** mapped_address
)
{
    if (!uefi_cpu_supports_pae())
    {
        uefi_debug_line(
            "[video] ERROR: CPU lacks PAE/PSE"
        );
        return false;
    }

    if (
        (uefi_read_cr0() & ROOTOS_CR0_PG) != 0u &&
        !uefi_pae_active
    )
    {
        uefi_debug_line(
            "[video] ERROR: unknown paging already active"
        );
        return false;
    }

    if (!uefi_pae_active)
    {
        uefi_build_identity_map();

        if (
            !uefi_install_framebuffer_window(
                physical_address,
                byte_length,
                mapped_address
            )
        )
        {
            return false;
        }

        u32 cr4 =
            uefi_read_cr4();

        cr4 |=
            ROOTOS_CR4_PAE |
            ROOTOS_CR4_PSE;

        uefi_write_cr4(cr4);

        uefi_write_cr3(
            (u32)(usize)&uefi_pdpt[0]
        );

        uefi_write_cr0(
            uefi_read_cr0() |
            ROOTOS_CR0_PG
        );

        /*
         * Current RootOS addresses remain identity mapped. A short branch
         * serializes instruction execution after paging becomes active.
         */
        __asm__ volatile(
            "jmp 1f\n"
            "1:"
            :
            :
            : "memory"
        );

        uefi_pae_active = true;
    }
    else
    {
        if (
            !uefi_install_framebuffer_window(
                physical_address,
                byte_length,
                mapped_address
            )
        )
        {
            return false;
        }

        uefi_write_cr3(
            (u32)(usize)&uefi_pdpt[0]
        );
    }

    return true;
}

/* ============================================================
 * ROOTDISPLAY INITIALIZATION
 * ============================================================ */

void rootdisplay_init(const MultibootInfo* multiboot)
{
    framebuffer = NULL;
    display_width = 0u;
    display_height = 0u;
    display_pitch = 0u;
    display_bpp = 0u;
    display_bytes_per_pixel = 0u;
    display_available = false;

    cursor_enabled = false;
    cursor_drawn = false;
    cursor_x = 0;
    cursor_y = 0;
    update_depth = 0u;

    uefi_debug_line(
        "[video] RootOS v0.47.6-1 rootdisplay init"
    );

    if (multiboot == NULL)
    {
        uefi_debug_line(
            "[video] ERROR: no Multiboot information"
        );
        return;
    }

    if (
        (multiboot->flags &
        MULTIBOOT_INFO_FRAMEBUFFER) == 0u
    )
    {
        uefi_debug_line(
            "[video] ERROR: framebuffer flag missing"
        );
        return;
    }

    uefi_debug_write(
        "[video] GOP/Multiboot fb="
    );
    uefi_debug_hex64(
        multiboot->framebuffer_addr
    );
    uefi_debug_write(" ");
    uefi_debug_u32(
        multiboot->framebuffer_width
    );
    uefi_debug_putc('x');
    uefi_debug_u32(
        multiboot->framebuffer_height
    );
    uefi_debug_putc('x');
    uefi_debug_u32(
        multiboot->framebuffer_bpp
    );
    uefi_debug_write(" pitch=");
    uefi_debug_u32(
        multiboot->framebuffer_pitch
    );
    uefi_debug_putc('\n');

    if (multiboot->framebuffer_type != 1u)
    {
        uefi_debug_line(
            "[video] ERROR: framebuffer is not direct RGB"
        );
        return;
    }

    if (
        multiboot->framebuffer_width == 0u ||
        multiboot->framebuffer_height == 0u
    )
    {
        uefi_debug_line(
            "[video] ERROR: zero framebuffer dimensions"
        );
        return;
    }

    if (
        multiboot->framebuffer_bpp != 24u &&
        multiboot->framebuffer_bpp != 32u
    )
    {
        uefi_debug_line(
            "[video] ERROR: unsupported framebuffer bpp"
        );
        return;
    }

    display_width =
        multiboot->framebuffer_width;

    display_height =
        multiboot->framebuffer_height;

    display_pitch =
        multiboot->framebuffer_pitch;

    display_bpp =
        multiboot->framebuffer_bpp;

    display_bytes_per_pixel =
        display_bpp /
        8u;

    u64 minimum_pitch =
        (u64)display_width *
        (u64)display_bytes_per_pixel;

    if ((u64)display_pitch < minimum_pitch)
    {
        uefi_debug_line(
            "[video] ERROR: framebuffer pitch too small"
        );
        return;
    }

    u64 framebuffer_bytes =
        (u64)display_pitch *
        (u64)display_height;

    if (
        framebuffer_bytes == 0u ||
        framebuffer_bytes > 0xFFFFFFFFULL
    )
    {
        uefi_debug_line(
            "[video] ERROR: invalid framebuffer byte size"
        );
        return;
    }

    if (multiboot->framebuffer_addr == 0u)
    {
        uefi_debug_line(
            "[video] ERROR: framebuffer address is zero"
        );
        return;
    }

    u64 framebuffer_last =
        multiboot->framebuffer_addr +
        framebuffer_bytes -
        1u;

    if (
        framebuffer_last <
        multiboot->framebuffer_addr
    )
    {
        uefi_debug_line(
            "[video] ERROR: framebuffer address overflow"
        );
        return;
    }

    if (framebuffer_last <= 0xFFFFFFFFULL)
    {
        framebuffer =
            (volatile u8*)(usize)
            multiboot->framebuffer_addr;

        uefi_debug_line(
            "[video] direct 32-bit framebuffer mapping"
        );
    }
    else
    {
        uefi_debug_line(
            "[video] framebuffer above 4 GiB -> PAE mapping"
        );

        if (
            !uefi_map_high_framebuffer(
                multiboot->framebuffer_addr,
                (usize)framebuffer_bytes,
                &framebuffer
            )
        )
        {
            framebuffer = NULL;

            uefi_debug_line(
                "[video] ERROR: PAE framebuffer mapping failed"
            );
            return;
        }

        uefi_debug_line(
            "[video] high framebuffer mapped at 0x80000000 window"
        );
    }

    red_position =
        multiboot->framebuffer_red_field_position;

    red_size =
        multiboot->framebuffer_red_mask_size;

    green_position =
        multiboot->framebuffer_green_field_position;

    green_size =
        multiboot->framebuffer_green_mask_size;

    blue_position =
        multiboot->framebuffer_blue_field_position;

    blue_size =
        multiboot->framebuffer_blue_mask_size;

    if (!rgb_layout_valid())
    {
        uefi_debug_line(
            "[video] WARNING: invalid RGB masks; using 8:8:8"
        );

        red_position = 16u;
        red_size = 8u;

        green_position = 8u;
        green_size = 8u;

        blue_position = 0u;
        blue_size = 8u;
    }

    display_available = true;

    uefi_debug_line(
        "[video] framebuffer ready"
    );
}
