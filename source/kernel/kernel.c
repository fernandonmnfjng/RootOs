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

static const char* boot_cmdline(
    u32 multiboot_magic,
    u32 multiboot_info_address
)
{
    if (
        multiboot_magic != MULTIBOOT_BOOTLOADER_MAGIC
        || multiboot_info_address == 0u
    )
    {
        return NULL;
    }

    const RootBootMultibootPrefix* info =
        (const RootBootMultibootPrefix*)(usize)multiboot_info_address;

    if (
        (info->flags & MULTIBOOT_INFO_CMDLINE_LOCAL) == 0u
        || info->cmdline == 0u
    )
    {
        return NULL;
    }

    return
        (const char*)(usize)info->cmdline;
}

static bool boot_text_contains(
    const char* text,
    const char* needle
)
{
    if (text == NULL || needle == NULL || needle[0] == '\0')
    {
        return false;
    }

    for (usize i = 0u; text[i] != '\0'; i++)
    {
        usize j = 0u;

        while (
            needle[j] != '\0'
            && text[i + j] == needle[j]
        )
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

static bool boot_safe_mode(
    u32 multiboot_magic,
    u32 multiboot_info_address
)
{
    return boot_text_contains(
        boot_cmdline(
            multiboot_magic,
            multiboot_info_address
        ),
        "rootos.boot=safe"
    );
}

static bool boot_full_probe_mode(
    u32 multiboot_magic,
    u32 multiboot_info_address
)
{
    return boot_text_contains(
        boot_cmdline(
            multiboot_magic,
            multiboot_info_address
        ),
        "rootos.probe=full"
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
        boot_safe_mode(
            multiboot_magic,
            multiboot_info_address
        );

    const bool full_probe_mode =
        boot_full_probe_mode(
            multiboot_magic,
            multiboot_info_address
        );

    /*
     * Minimum runtime needed to get pixels/text on screen. Keep this before
     * every hardware discovery so failures leave a visible last stage.
     */
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

    if (safe_mode)
    {
        terminal_print("[boot] mode: SAFE\n");
    }
    else if (full_probe_mode)
    {
        terminal_print("[boot] mode: HARDWARE PROBE DIAGNOSTIC\n");
    }
    else
    {
        terminal_print("[boot] mode: NORMAL (bounded discovery)\n");
    }

    /*
     * Core input / interrupt platform.
     */
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

    /*
     * Hardware policy
     * ---------------
     *
     * NORMAL:
     *   Enumerate PCI and register known bus/class drivers, but do not perform
     *   aggressive device resets, dynamic driver binding or legacy ATA probes
     *   before the shell exists.
     *
     * FULL PROBE DIAGNOSTIC:
     *   Runs the old aggressive binding/storage path intentionally so a faulty
     *   driver can be isolated without making it the default boot behavior.
     *
     * SAFE:
     *   No PCI/USB/device probing.
     */
    if (!safe_mode)
    {
        boot_stage("PCI discovery");
        pci_init();
        device_manager_init();
        boot_stage_ok("PCI discovery");

        boot_stage("USB registry");
        usb_init();

        /*
         * xhci_init() in the current RootOS driver is discovery-only: it
         * registers/binds the PCI class driver but does not reset/start the
         * controller. Keep that safe behavior during default boot.
         */
        xhci_init();

        block_device_init();
        usb_mass_storage_init();
        boot_stage_ok("USB registry");

        boot_stage("network registries");
        net_device_init();
        rndis_init();
        net_init();
        boot_stage_ok("network registries");

        /*
         * xhci_init() only discovers/registers the host controller.
         * Start it automatically in normal mode so USB devices are actually
         * enumerated. Every xHCI wait is bounded; failure is non-fatal and the
         * shell still starts.
         */
        boot_stage("USB hardware");

        if (xhci_controller_count() == 0u)
        {
            terminal_print(
                "[boot] USB hardware: no xHCI controller detected\n"
            );
        }
        else
        {
            (void)xhci_start();

            if (xhci_any_running())
            {
                /*
                 * Deliver deferred device-added notifications now that Mass
                 * Storage and RNDIS listeners have been registered.
                 */
                usb_service();
                boot_stage_ok("USB hardware");
            }
            else
            {
                terminal_print(
                    "[boot] USB hardware: xHCI start failed; continuing\n"
                );
            }
        }

        boot_stage("driver store");
        driver_store_init(
            multiboot_magic,
            multiboot_info_address
        );

        if (full_probe_mode)
        {
            terminal_print(
                "[boot] full probe: dynamic driver binding enabled\n"
            );

            driver_store_bind_all();
        }
        else
        {
            terminal_print(
                "[boot] dynamic driver binding: DEFERRED\n"
            );
        }

        boot_stage_ok("driver store");

        if (full_probe_mode)
        {
            boot_stage("legacy ATA storage probe");

            if (rootstorage_init())
            {
                boot_stage_ok("legacy ATA storage probe");
            }
            else
            {
                terminal_print(
                    "[boot] legacy ATA storage probe: unavailable\n"
                );
            }
        }
        else
        {
            terminal_print(
                "[boot] legacy ATA storage probe: DEFERRED\n"
            );
        }
    }
    else
    {
        /*
         * Pure registries are safe and keep shell commands predictable.
         */
        block_device_init();
        net_device_init();
        net_init();

        terminal_print(
            "[boot] hardware discovery: SKIPPED\n"
        );
    }

    /*
     * Filesystem and application platform.
     * Without a recognized disk backend filesystem_init() intentionally falls
     * back to the temporary RAM tree.
     */
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
    else if (!full_probe_mode)
    {
        terminal_print(
            "NORMAL MODE: risky device activation is deferred until after boot.\n"
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
