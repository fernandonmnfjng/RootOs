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
#include "rootstorage.h"
#include "process.h"
#include "rootapi.h"
#include "app_manager.h"
#include "package_manager.h"
#include "pci.h"
#include "device_manager.h"
#include "usb.h"
#include "xhci.h"
#include "block_device.h"
#include "usb_mass_storage.h"
#include "rndis.h"
#include "net_device.h"
#include "driver_store.h"
#include "net.h"

#define MULTIBOOT_INFO_CMDLINE_LOCAL (1u << 2)

/* Only the fixed Multiboot v1 prefix needed for boot-option parsing. */
typedef struct __attribute__((packed))
{
    u32 flags;
    u32 mem_lower;
    u32 mem_upper;
    u32 boot_device;
    u32 cmdline;
} RootBootMultibootPrefix;

static bool boot_text_contains(const char* text, const char* needle)
{
    if (text == NULL || needle == NULL || needle[0] == '\0')
    {
        return false;
    }

    for (usize i = 0u; text[i] != '\0'; i++)
    {
        usize j = 0u;
        while (needle[j] != '\0' && text[i + j] == needle[j])
        {
            j++;
        }
        if (needle[j] == '\0')
        {
            return true;
        }
    }

    return false;
}

static bool boot_safe_mode(u32 multiboot_magic, u32 multiboot_info_address)
{
    if (
        multiboot_magic != MULTIBOOT_BOOTLOADER_MAGIC
        || multiboot_info_address == 0u
    )
    {
        return false;
    }

    const RootBootMultibootPrefix* info =
        (const RootBootMultibootPrefix*)(usize)multiboot_info_address;

    if (
        (info->flags & MULTIBOOT_INFO_CMDLINE_LOCAL) == 0u
        || info->cmdline == 0u
    )
    {
        return false;
    }

    return boot_text_contains(
        (const char*)(usize)info->cmdline,
        "rootos.boot=safe"
    );
}

static void boot_stage(const char* stage)
{
    terminal_print("[boot] ");
    terminal_print(stage);
    terminal_print("...\n");
}

static void boot_stage_ok(const char* stage)
{
    terminal_print("[boot] ");
    terminal_print(stage);
    terminal_print(": OK\n");
}

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
    const bool safe_mode =
        boot_safe_mode(multiboot_magic, multiboot_info_address);

    /* --------------------------------------------------------
     * Minimum runtime needed to get pixels/text on screen.
     * Keep this before every hardware probe so physical boot
     * failures leave a visible last stage.
     * -------------------------------------------------------- */

    heap_init();

    if (
        multiboot_magic == MULTIBOOT_BOOTLOADER_MAGIC
        && multiboot_info_address != 0u
    )
    {
        rootdisplay_init(
            (const MultibootInfo*)(usize)multiboot_info_address
        );
    }

    terminal_init();
    terminal_clear();

    terminal_print("RootOS ");
    terminal_print(ROOTOS_VERSION_STRING);
    terminal_print(" early boot\n");
    terminal_print("[boot] kernel entry: OK\n");

    if (rootdisplay_ready())
    {
        terminal_print("[boot] framebuffer: Multiboot RGB\n");
    }
    else
    {
        terminal_print("[boot] framebuffer: VGA fallback\n");
    }

    terminal_print(
        safe_mode
            ? "[boot] mode: SAFE (hardware probing disabled)\n"
            : "[boot] mode: NORMAL\n"
    );

    /* --------------------------------------------------------
     * Core input / interrupt platform.
     * -------------------------------------------------------- */

    boot_stage("clipboard");
    rootclipboard_init();
    boot_stage_ok("clipboard");

    boot_stage("input");
    rootinput_init();
    boot_stage_ok("input");

    boot_stage("interrupts");
    interrupts_init();
    boot_stage_ok("interrupts");

    boot_stage("clock");
    pit_init(PIT_DEFAULT_FREQUENCY);
    boot_stage_ok("clock");

    interrupts_enable();
    terminal_print("[boot] interrupts enabled\n");

    /* --------------------------------------------------------
     * Hardware-dependent path.
     * Safe mode deliberately avoids PCI, USB, dynamic drivers,
     * NICs and disk probes so a bad device cannot prevent shell
     * access on a new machine.
     * -------------------------------------------------------- */

    if (!safe_mode)
    {
        boot_stage("PCI");
        pci_init();
        device_manager_init();
        boot_stage_ok("PCI");

        boot_stage("USB core");
        usb_init();
        xhci_init();
        block_device_init();
        usb_mass_storage_init();
        boot_stage_ok("USB core");

        boot_stage("network registries");
        net_device_init();
        rndis_init();
        boot_stage_ok("network registries");

        boot_stage("driver store");
        driver_store_init(multiboot_magic, multiboot_info_address);
        driver_store_bind_all();
        boot_stage_ok("driver store");

        boot_stage("network stack");
        net_init();
        boot_stage_ok("network stack");

        boot_stage("storage probe");
        rootstorage_init();
        boot_stage_ok("storage probe");
    }
    else
    {
        /* Pure registries are safe and keep shell commands predictable. */
        block_device_init();
        net_device_init();
        net_init();
        terminal_print("[boot] hardware probes: SKIPPED\n");
    }

    /* --------------------------------------------------------
     * Filesystem and application platform.
     * Without a recognized disk backend filesystem_init()
     * intentionally falls back to the temporary RAM tree.
     * -------------------------------------------------------- */

    boot_stage("filesystem");
    filesystem_init();
    boot_stage_ok("filesystem");

    boot_stage("application platform");
    process_manager_init();
    rootapi_init();
    app_manager_init();
    package_manager_init();
    boot_stage_ok("application platform");

    terminal_putchar('\n');
    terminal_print(ROOTOS_NAME);
    terminal_print(" v");
    terminal_print(ROOTOS_VERSION_STRING);
    terminal_print(" - ");
    terminal_print(ROOTOS_BUILD_TYPE);
    terminal_putchar('\n');
    terminal_print(ROOTOS_BANNER_SEPARATOR);
    terminal_putchar('\n');
    terminal_print(ROOTOS_BANNER_HELP);
    terminal_putchar('\n');

    if (safe_mode)
    {
        terminal_print(
            "SAFE MODE: PCI/USB/network/storage probing was disabled.\n"
        );
    }

    terminal_putchar('\n');

    shell_run();

    interrupts_disable();
    while (1)
    {
        __asm__ volatile("hlt");
    }
}
