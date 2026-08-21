/*
 * RootOS v0.47.6-3 buffered framebuffer + hardware hotfix.
 *
 * The installer preserves the original RootOS rootdisplay.c as:
 *
 *     rootdisplay_legacy_impl.c
 *
 * We compile that implementation in this translation unit as an internal
 * backend. All existing drawing, font, cursor, scrolling and selection code
 * stays intact; the public layer adds buffered/damage-based presentation.
 *
 * The replacement rootdisplay_init() below adds the missing case for UEFI GOP
 * framebuffers whose physical address/range is above 4 GiB.  RootOS remains an
 * i386 kernel; a minimal PAE mapping exposes that physical range through a
 * fixed 512 MiB virtual framebuffer window. When the CPU supports PAT,
 * the framebuffer is mapped Write-Combining (WC), matching the strategy
 * used by mature x86 kernels for linear framebuffers.
 */

/*
 * Compile the established renderer as an internal backend. Public calls below
 * add a cached shadow framebuffer and damage flushing without rewriting the
 * proven glyph/cursor/scroll implementation.
 */
#define rootdisplay_init             legacy_rootdisplay_init
#define rootdisplay_ready            legacy_rootdisplay_ready
#define rootdisplay_width            legacy_rootdisplay_width
#define rootdisplay_height           legacy_rootdisplay_height
#define rootdisplay_rgb              legacy_rootdisplay_rgb
#define rootdisplay_put_pixel        legacy_rootdisplay_put_pixel
#define rootdisplay_get_pixel        legacy_rootdisplay_get_pixel
#define rootdisplay_draw_mono_bitmap legacy_rootdisplay_draw_mono_bitmap
#define rootdisplay_fill_rect        legacy_rootdisplay_fill_rect
#define rootdisplay_invert_rect      legacy_rootdisplay_invert_rect
#define rootdisplay_begin_update     legacy_rootdisplay_begin_update
#define rootdisplay_end_update       legacy_rootdisplay_end_update
#define rootdisplay_scroll_up        legacy_rootdisplay_scroll_up
#define rootdisplay_shift_vertical   legacy_rootdisplay_shift_vertical
#define rootdisplay_clear            legacy_rootdisplay_clear
#define rootdisplay_cursor_enable    legacy_rootdisplay_cursor_enable
#define rootdisplay_cursor_move      legacy_rootdisplay_cursor_move
#define rootdisplay_cursor_visible   legacy_rootdisplay_cursor_visible

#include "rootdisplay_legacy_impl.c"

#undef rootdisplay_init
#undef rootdisplay_ready
#undef rootdisplay_width
#undef rootdisplay_height
#undef rootdisplay_rgb
#undef rootdisplay_put_pixel
#undef rootdisplay_get_pixel
#undef rootdisplay_draw_mono_bitmap
#undef rootdisplay_fill_rect
#undef rootdisplay_invert_rect
#undef rootdisplay_begin_update
#undef rootdisplay_end_update
#undef rootdisplay_scroll_up
#undef rootdisplay_shift_vertical
#undef rootdisplay_clear
#undef rootdisplay_cursor_enable
#undef rootdisplay_cursor_move
#undef rootdisplay_cursor_visible

#include "io.h"

static u32 uefi_cpu_features_edx(void);

/* ============================================================
 * CACHED SHADOW FRAMEBUFFER
 * ============================================================
 *
 * Direct reads from PCI/firmware framebuffers are painfully slow on real
 * hardware. The established RootOS renderer now draws into normal cached RAM.
 * At the end of a render transaction only the damaged rectangle is copied,
 * sequentially, to the physical GOP framebuffer.
 *
 * GRUB currently prefers 1024x768x32 (~3 MiB). 16 MiB also covers common
 * 1920x1080 modes with normal pitch. Larger modes fall back to direct drawing
 * rather than refusing to boot.
 */

#define ROOTOS_SHADOW_FRAMEBUFFER_BYTES (16u * 1024u * 1024u)

static u8 rootdisplay_shadow_framebuffer[
    ROOTOS_SHADOW_FRAMEBUFFER_BYTES
] __attribute__((aligned(64)));

static volatile u8* rootdisplay_physical_framebuffer = NULL;
static usize rootdisplay_framebuffer_bytes = 0u;
static bool rootdisplay_shadow_active = false;

static bool rootdisplay_dirty = false;
static u32 rootdisplay_dirty_x0 = 0u;
static u32 rootdisplay_dirty_y0 = 0u;
static u32 rootdisplay_dirty_x1 = 0u; /* exclusive */
static u32 rootdisplay_dirty_y1 = 0u; /* exclusive */

static void rootdisplay_damage_reset(void)
{
    rootdisplay_dirty = false;
    rootdisplay_dirty_x0 = 0u;
    rootdisplay_dirty_y0 = 0u;
    rootdisplay_dirty_x1 = 0u;
    rootdisplay_dirty_y1 = 0u;
}

static void rootdisplay_damage(
    u32 x,
    u32 y,
    u32 width,
    u32 height
)
{
    if (
        !rootdisplay_shadow_active ||
        width == 0u ||
        height == 0u ||
        x >= display_width ||
        y >= display_height
    )
    {
        return;
    }

    u32 x1 = x + width;
    u32 y1 = y + height;

    if (x1 < x || x1 > display_width)
        x1 = display_width;

    if (y1 < y || y1 > display_height)
        y1 = display_height;

    if (!rootdisplay_dirty)
    {
        rootdisplay_dirty = true;
        rootdisplay_dirty_x0 = x;
        rootdisplay_dirty_y0 = y;
        rootdisplay_dirty_x1 = x1;
        rootdisplay_dirty_y1 = y1;
        return;
    }

    if (x < rootdisplay_dirty_x0)
        rootdisplay_dirty_x0 = x;

    if (y < rootdisplay_dirty_y0)
        rootdisplay_dirty_y0 = y;

    if (x1 > rootdisplay_dirty_x1)
        rootdisplay_dirty_x1 = x1;

    if (y1 > rootdisplay_dirty_y1)
        rootdisplay_dirty_y1 = y1;
}

static void rootdisplay_mmio_copy(
    volatile u8* destination,
    const u8* source,
    usize bytes
)
{
    while (
        bytes != 0u &&
        ((((usize)destination) | ((usize)source)) & 3u) != 0u
    )
    {
        *destination++ = *source++;
        bytes--;
    }

    volatile u32* dst32 = (volatile u32*)destination;
    const u32* src32 = (const u32*)source;

    while (bytes >= 32u)
    {
        dst32[0] = src32[0];
        dst32[1] = src32[1];
        dst32[2] = src32[2];
        dst32[3] = src32[3];
        dst32[4] = src32[4];
        dst32[5] = src32[5];
        dst32[6] = src32[6];
        dst32[7] = src32[7];

        dst32 += 8;
        src32 += 8;
        bytes -= 32u;
    }

    while (bytes >= 4u)
    {
        *dst32++ = *src32++;
        bytes -= 4u;
    }

    destination = (volatile u8*)dst32;
    source = (const u8*)src32;

    while (bytes != 0u)
    {
        *destination++ = *source++;
        bytes--;
    }
}

static void rootdisplay_flush_damage(void)
{
    if (
        !rootdisplay_shadow_active ||
        !rootdisplay_dirty ||
        rootdisplay_physical_framebuffer == NULL ||
        framebuffer == NULL
    )
    {
        return;
    }

    u32 x0 = rootdisplay_dirty_x0;
    u32 x1 = rootdisplay_dirty_x1;
    u32 y0 = rootdisplay_dirty_y0;
    u32 y1 = rootdisplay_dirty_y1;

    rootdisplay_damage_reset();

    if (
        x0 >= x1 ||
        y0 >= y1 ||
        x1 > display_width ||
        y1 > display_height
    )
    {
        return;
    }

    usize byte_x =
        (usize)x0 *
        (usize)display_bytes_per_pixel;

    usize row_bytes =
        (usize)(x1 - x0) *
        (usize)display_bytes_per_pixel;

    for (u32 y = y0; y < y1; y++)
    {
        usize offset =
            (usize)y *
            (usize)display_pitch +
            byte_x;

        rootdisplay_mmio_copy(
            rootdisplay_physical_framebuffer + offset,
            ((const u8*)framebuffer) + offset,
            row_bytes
        );
    }

    /*
     * WC stores may be buffered. SFENCE is available with SSE; every CPU on
     * which we enable PAT in practice has it, but retain the feature check for
     * old x86 hardware.
     */
    if ((uefi_cpu_features_edx() & (1u << 25)) != 0u)
        __asm__ volatile("sfence" ::: "memory");
    else
        __asm__ volatile("" ::: "memory");
}

static void rootdisplay_flush_if_idle(void)
{
    if (update_depth == 0u)
        rootdisplay_flush_damage();
}


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

/* PAT index for a 2 MiB PDE is selected by PAT(bit 12), PCD and PWT.
 * RootOS programs PAT entry 1 as WC and selects it with PWT=1. */
#define ROOTOS_PAE_PAT_LARGE (1ULL << 12)

#define ROOTOS_MSR_IA32_PAT 0x00000277u
#define ROOTOS_PAT_TYPE_UC  0x00u
#define ROOTOS_PAT_TYPE_WC  0x01u
#define ROOTOS_PAT_TYPE_WT  0x04u
#define ROOTOS_PAT_TYPE_WP  0x05u
#define ROOTOS_PAT_TYPE_WB  0x06u
#define ROOTOS_PAT_TYPE_UCM 0x07u

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
static bool uefi_pat_wc_active = false;

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

static u32 uefi_cpu_features_edx(void)
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
        return 0u;

    uefi_cpuid(
        1u,
        NULL,
        NULL,
        NULL,
        &features_edx
    );

    return features_edx;
}

static bool uefi_cpu_supports_pae(void)
{
    const u32 features_edx =
        uefi_cpu_features_edx();

    /* CPUID.01H:EDX.PSE[3], PAE[6]. */
    const u32 required =
        (1u << 3) |
        (1u << 6);

    return (features_edx & required) == required;
}

static bool uefi_cpu_supports_pat(void)
{
    const u32 features_edx =
        uefi_cpu_features_edx();

    /* CPUID.01H:EDX.MSR[5], PAT[16]. */
    const u32 required =
        (1u << 5) |
        (1u << 16);

    return (features_edx & required) == required;
}

static u64 uefi_read_msr(u32 msr)
{
    u32 low;
    u32 high;

    __asm__ volatile(
        "rdmsr"
        : "=a"(low), "=d"(high)
        : "c"(msr)
    );

    return
        (u64)low |
        ((u64)high << 32);
}

static void uefi_write_msr(
    u32 msr,
    u64 value
)
{
    __asm__ volatile(
        "wrmsr"
        :
        : "c"(msr),
          "a"((u32)(value & 0xFFFFFFFFu)),
          "d"((u32)(value >> 32))
        : "memory"
    );
}

static bool uefi_enable_pat_write_combining(void)
{
    if (!uefi_cpu_supports_pat())
        return false;

    /*
     * PAT entry 1 is selected by PWT=1, PCD=0, PAT=0.
     *
     * The architectural reset value normally makes entry 1 WT. RootOS owns
     * all page tables at this point and its identity map does not use PWT, so
     * changing only this entry to WC cannot alter ordinary kernel RAM.
     */
    u64 pat =
        uefi_read_msr(
            ROOTOS_MSR_IA32_PAT
        );

    pat &=
        ~(0xFFULL << 8);

    pat |=
        (u64)ROOTOS_PAT_TYPE_WC <<
        8;

    uefi_write_msr(
        ROOTOS_MSR_IA32_PAT,
        pat
    );

    /* CPUID serializes execution after the PAT MSR update. */
    uefi_cpuid(
        0u,
        NULL,
        NULL,
        NULL,
        NULL
    );

    return true;
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
         * Linear framebuffer:
         *
         * - PAT available: entry 1 is programmed WC. PWT=1 selects it.
         * - PAT unavailable: PCD+PWT selects the architectural UC entry.
         *
         * Never cache device framebuffer memory as normal WB RAM.
         */
        u64 cache_flags =
            uefi_pat_wc_active
            ?
            ROOTOS_PAE_PWT
            :
            (ROOTOS_PAE_PWT | ROOTOS_PAE_PCD);

        uefi_page_directories[
            pdpt_index
        ][
            first_pde + index
        ] =
            (physical & ROOTOS_PAE_PDE_ADDRESS_MASK) |
            ROOTOS_PAE_PRESENT |
            ROOTOS_PAE_RW |
            cache_flags |
            ROOTOS_PAE_PS;
    }

    *mapped_address =
        (volatile u8*)(usize)(
            ROOTOS_FB_WINDOW_BASE +
            (u32)page_offset
        );

    return true;
}

static bool uefi_map_framebuffer(
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
        uefi_pat_wc_active =
            uefi_enable_pat_write_combining();

        if (uefi_pat_wc_active)
        {
            uefi_debug_line(
                "[video] PAT framebuffer cache: write-combining"
            );
        }
        else
        {
            uefi_debug_line(
                "[video] PAT unavailable: uncached framebuffer fallback"
            );
        }

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
    rootdisplay_physical_framebuffer = NULL;
    rootdisplay_framebuffer_bytes = 0u;
    rootdisplay_shadow_active = false;
    rootdisplay_damage_reset();

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
        "[video] RootOS v0.47.6-3 rootdisplay init"
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

    /*
     * Prefer the PAE aperture even for a framebuffer below 4 GiB so RootOS can
     * apply an explicit framebuffer cache policy (WC when PAT exists).
     *
     * Very old CPUs without PAE retain the original direct 32-bit path, which
     * preserves BIOS-era compatibility.
     */
    if (uefi_cpu_supports_pae())
    {
        if (
            !uefi_map_framebuffer(
                multiboot->framebuffer_addr,
                (usize)framebuffer_bytes,
                &rootdisplay_physical_framebuffer
            )
        )
        {
            rootdisplay_physical_framebuffer = NULL;

            uefi_debug_line(
                "[video] ERROR: PAE framebuffer mapping failed"
            );
            return;
        }

        uefi_debug_line(
            "[video] framebuffer mapped through RootOS PAE aperture"
        );
    }
    else if (framebuffer_last <= 0xFFFFFFFFULL)
    {
        rootdisplay_physical_framebuffer =
            (volatile u8*)(usize)
            multiboot->framebuffer_addr;

        uefi_debug_line(
            "[video] legacy direct framebuffer mapping (no PAE)"
        );
    }
    else
    {
        uefi_debug_line(
            "[video] ERROR: high framebuffer requires PAE"
        );
        return;
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

    rootdisplay_framebuffer_bytes =
        (usize)framebuffer_bytes;

    if (
        rootdisplay_physical_framebuffer != NULL &&
        rootdisplay_framebuffer_bytes <=
            ROOTOS_SHADOW_FRAMEBUFFER_BYTES
    )
    {
        for (
            usize i = 0u;
            i < rootdisplay_framebuffer_bytes;
            i++
        )
        {
            rootdisplay_shadow_framebuffer[i] = 0u;
        }

        framebuffer =
            (volatile u8*)
            &rootdisplay_shadow_framebuffer[0];

        rootdisplay_shadow_active = true;

        uefi_debug_line(
            "[video] cached shadow framebuffer: enabled"
        );
    }
    else
    {
        framebuffer =
            rootdisplay_physical_framebuffer;

        rootdisplay_shadow_active = false;

        uefi_debug_line(
            "[video] cached shadow framebuffer: direct fallback"
        );
    }

    display_available = true;

    uefi_debug_line(
        "[video] framebuffer ready"
    );
}

/* ============================================================
 * PUBLIC BUFFERED DISPLAY API
 * ============================================================ */

bool rootdisplay_ready(void)
{
    return legacy_rootdisplay_ready();
}

u32 rootdisplay_width(void)
{
    return legacy_rootdisplay_width();
}

u32 rootdisplay_height(void)
{
    return legacy_rootdisplay_height();
}

u32 rootdisplay_rgb(u8 red, u8 green, u8 blue)
{
    return legacy_rootdisplay_rgb(red, green, blue);
}

void rootdisplay_begin_update(void)
{
    legacy_rootdisplay_begin_update();
}

void rootdisplay_end_update(void)
{
    legacy_rootdisplay_end_update();
    rootdisplay_flush_if_idle();
}

void rootdisplay_put_pixel(
    u32 x,
    u32 y,
    u32 color
)
{
    rootdisplay_damage(x, y, 1u, 1u);
    legacy_rootdisplay_put_pixel(x, y, color);
    rootdisplay_flush_if_idle();
}

u32 rootdisplay_get_pixel(u32 x, u32 y)
{
    return legacy_rootdisplay_get_pixel(x, y);
}

void rootdisplay_draw_mono_bitmap(
    u32 x,
    u32 y,
    u32 width,
    u32 height,
    const u8* bitmap,
    u32 stride_bytes,
    u32 foreground,
    u32 background,
    bool opaque
)
{
    rootdisplay_damage(x, y, width, height);

    legacy_rootdisplay_draw_mono_bitmap(
        x,
        y,
        width,
        height,
        bitmap,
        stride_bytes,
        foreground,
        background,
        opaque
    );

    rootdisplay_flush_if_idle();
}

void rootdisplay_fill_rect(
    u32 x,
    u32 y,
    u32 width,
    u32 height,
    u32 color
)
{
    rootdisplay_damage(x, y, width, height);

    legacy_rootdisplay_fill_rect(
        x,
        y,
        width,
        height,
        color
    );

    rootdisplay_flush_if_idle();
}

void rootdisplay_invert_rect(
    u32 x,
    u32 y,
    u32 width,
    u32 height
)
{
    rootdisplay_damage(x, y, width, height);

    legacy_rootdisplay_invert_rect(
        x,
        y,
        width,
        height
    );

    rootdisplay_flush_if_idle();
}

void rootdisplay_scroll_up(
    u32 pixel_rows,
    u32 fill_color
)
{
    rootdisplay_damage(
        0u,
        0u,
        display_width,
        display_height
    );

    legacy_rootdisplay_scroll_up(
        pixel_rows,
        fill_color
    );

    rootdisplay_flush_if_idle();
}

void rootdisplay_shift_vertical(
    u32 region_y,
    u32 region_height,
    i32 pixel_delta,
    u32 fill_color
)
{
    rootdisplay_damage(
        0u,
        region_y,
        display_width,
        region_height
    );

    legacy_rootdisplay_shift_vertical(
        region_y,
        region_height,
        pixel_delta,
        fill_color
    );

    rootdisplay_flush_if_idle();
}

void rootdisplay_clear(u32 color)
{
    rootdisplay_damage(
        0u,
        0u,
        display_width,
        display_height
    );

    legacy_rootdisplay_clear(color);
    rootdisplay_flush_if_idle();
}

void rootdisplay_cursor_enable(bool enabled)
{
    if (display_available)
    {
        rootdisplay_damage(
            (u32)(cursor_x < 0 ? 0 : cursor_x),
            (u32)(cursor_y < 0 ? 0 : cursor_y),
            ROOT_CURSOR_WIDTH,
            ROOT_CURSOR_HEIGHT
        );
    }

    legacy_rootdisplay_cursor_enable(enabled);

    if (display_available)
    {
        rootdisplay_damage(
            (u32)(cursor_x < 0 ? 0 : cursor_x),
            (u32)(cursor_y < 0 ? 0 : cursor_y),
            ROOT_CURSOR_WIDTH,
            ROOT_CURSOR_HEIGHT
        );
    }

    rootdisplay_flush_if_idle();
}

void rootdisplay_cursor_move(i32 x, i32 y)
{
    i32 old_x = cursor_x;
    i32 old_y = cursor_y;

    if (display_available)
    {
        rootdisplay_damage(
            (u32)(old_x < 0 ? 0 : old_x),
            (u32)(old_y < 0 ? 0 : old_y),
            ROOT_CURSOR_WIDTH,
            ROOT_CURSOR_HEIGHT
        );
    }

    legacy_rootdisplay_cursor_move(x, y);

    if (display_available)
    {
        rootdisplay_damage(
            (u32)(cursor_x < 0 ? 0 : cursor_x),
            (u32)(cursor_y < 0 ? 0 : cursor_y),
            ROOT_CURSOR_WIDTH,
            ROOT_CURSOR_HEIGHT
        );
    }

    rootdisplay_flush_if_idle();
}

bool rootdisplay_cursor_visible(void)
{
    return legacy_rootdisplay_cursor_visible();
}
