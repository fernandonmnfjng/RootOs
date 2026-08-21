#include "shell.h"

#include "terminal.h"
#include "rootinput.h"
#include "rootclipboard.h"
#include "roottext.h"
#include "rootedit.h"

#include "unicode.h"

#include "filesystem.h"
#include "path.h"

#include "io.h"

#include "string.h"
#include "memory.h"

#include "system_config.h"
#include "rootstorage.h"
#include "process.h"
#include "app_manager.h"
#include "elf_loader.h"
#include "package_manager.h"
#include "pci.h"
#include "device_manager.h"
#include "usb.h"
#include "xhci.h"
#include "usb_mass_storage.h"
#include "rndis.h"
#include "block_device.h"
#include "net_device.h"
#include "driver_store.h"
#include "net.h"
#include "arp.h"
#include "dhcp.h"
#include "dns.h"
#include "tcp.h"
#include "time.h"


#define COMMAND_BUFFER_SIZE 128

#define COMMAND_UTF8_SIZE \
    ((COMMAND_BUFFER_SIZE * 4) + 1)


static RootCodepoint command_buffer[
    COMMAND_BUFFER_SIZE
];


static char command_utf8[
    COMMAND_UTF8_SIZE
];


static u32 command_length = 0;

static u32 command_cursor = 0;


/*
 * Celdas visuales, no bytes.
 */
static u32 rendered_length = 0;


static u32 input_start_row = 0;

static u32 input_start_col = 0;

/*
 * ============================================================
 * COMMAND HISTORY
 * ============================================================
 */

#define SHELL_HISTORY_SIZE 32


static RootCodepoint command_history[
    SHELL_HISTORY_SIZE
][
    COMMAND_BUFFER_SIZE
];


static u32 command_history_length[
    SHELL_HISTORY_SIZE
];


static u32 command_history_count =
    0;


static i32 command_history_position =
    -1;


#define SHELL_RUN_MAX_ARGS 16u

static char run_arg_storage[SHELL_RUN_MAX_ARGS][COMMAND_UTF8_SIZE];
static const char* run_argv[SHELL_RUN_MAX_ARGS];


static bool shell_read_argument(
    const char** cursor,
    char* output,
    usize output_size
);

/*
 * =====================================
 * COMANDOS
 * =====================================
 */

static void command_help(void)
{
    terminal_print(
        "RootOS commands:\n\n"
    );


    terminal_print(
        "help                         Show commands\n"
    );


    terminal_print(
        "clear                        Clear terminal\n"
    );


    terminal_print(
        "about                        Show RootOS information\n"
    );


    terminal_print(
        "echo <text>                  Print text\n"
    );


    terminal_print(
        "godir <path>                 Change directory\n"
    );


    terminal_print(
        "seedir                       Show current directory\n"
    );


    terminal_print(
        "seedir(\"name\")               Find directories\n"
    );


    terminal_print(
        "see                           List current directory\n"
    );


    terminal_print(
        "see <path>                    List directory\n"
    );


    terminal_print(
        "create --file <path>          Create file\n"
    );


    terminal_print(
        "create --folder <path>        Create folder\n"
    );


    terminal_print(
        "remove <path>                 Remove file/folder\n"
    );


    terminal_print(
        "remove -r <path>              Remove recursively\n"
    );


    terminal_print(
        "copy <source> <destination>   Copy\n"
    );


    terminal_print(
        "move <source> <destination>   Move\n"
    );


    terminal_print(
        "readfile <path>               Read text file\n"
    );


    terminal_print(
        "writefile <path> \"text\"      Replace file content\n"
    );


    terminal_print(
        "appendfile <path> \"text\"     Append file content\n"
    );


    terminal_print(
        "editfile <path>               Open RootEdit\n"
    );


    terminal_print(
        "storage                       Show persistent storage status\n"
    );


    terminal_print(
        "pci [rescan]                  List/rescan PCI devices\n"
    );


    terminal_print(
        "devices                       List Device Manager inventory\n"
    );


    terminal_print(
        "device info <id>              Show device details/BARs\n"
    );


    terminal_print(
        "drivers                       List built-in and Driver Pack drivers\n"
        "driverpack                    Show boot Driver Store contents\n"
    );


    terminal_print(
        "usb                           List enumerated USB devices\n"
    );


    terminal_print(
        "usb info <id>                 Show USB descriptors/interfaces\n"
    );


    terminal_print(
        "usb ports                     Show xHCI root-port status\n"
        "usb health                    Show USB recovery/hotplug counters\n"
    );


    terminal_print(
        "usb start                     Initialize detected xHCI controller(s)\n"
        "usb controllers               Show xHCI controller details/state\n"
    );


    terminal_print(
        "usb rescan                    Re-enumerate connected USB devices\n"
    );

    terminal_print(
        "usb storage                   Probe USB Mass Storage devices\n"
        "rndis                        Probe/show USB RNDIS adapters\n"
    );

    terminal_print(
        "disks                         List block devices\n"
    );

    terminal_print(
        "disk info <id>                Show block device details\n"
    );

    terminal_print(
        "disk read <id> <lba>          Read one block (hex preview)\n"
    );

    terminal_print(
        "net                           Show active network status\n"
        "net devices                   List network adapters\n"
        "net use <id>                  Select active network adapter\n"
    );

    terminal_print(
        "ifconfig                      Show IPv4 configuration\n"
    );

    terminal_print(
        "dhcp                          Acquire IPv4 configuration\n"
    );

    terminal_print(
        "arp [ipv4]                    Show ARP table/request address\n"
    );

    terminal_print(
        "dns <name>                    Resolve DNS A record\n"
        "dns cache                     Show DNS cache\n"
        "dns flush                     Clear DNS cache\n"
    );

    terminal_print(
        "tcp connect <host> <port>     Test TCP three-way handshake\n"
        "tcp list                      Show TCP connections\n"
        "tcp close <id>                Close/remove TCP test connection\n"
    );


    terminal_print(
        "run <app|file.elf> [args]     Run a RootOS ELF application\n"
    );


    terminal_print(
        "ps                            Show process history\n"
    );


    terminal_print(
        "package install <file.rtpgk>  Install local package\n"
    );


    terminal_print(
        "package remove <name>         Remove package\n"
    );


    terminal_print(
        "package list                  List installed packages\n"
    );


    terminal_print(
        "package info <name>           Show package information\n"
    );


    terminal_print(
        "reboot                        Reboot\n"
    );


    terminal_print(
        "shutdown                      Shutdown QEMU\n"
    );
}


static void command_about(void)
{
    terminal_print(
        ROOTOS_NAME
    );

    terminal_print(
        " v"
    );

    terminal_print(
        ROOTOS_VERSION_STRING
    );

    terminal_putchar('\n');


    terminal_print(
        "Build: "
    );

    terminal_print(
        ROOTOS_BUILD_TYPE
    );

    terminal_putchar('\n');


    terminal_print(
        "Architecture: x86 32-bit\n"
    );


    terminal_print(
        "Kernel: Root Kernel\n"
    );


    terminal_print(
        "Boot protocol: GRUB Multiboot\n"
    );


    terminal_print(
        "Default home: "
    );

    terminal_print(
        ROOTOS_DEFAULT_HOME
    );

    terminal_putchar('\n');
}


static void command_echo(
    const char* text
)
{
    terminal_print(text);

    terminal_putchar('\n');
}


/*
 * godir
 */
static void command_godir(
    const char* path
)
{
    if (
        filesystem_change_directory(
            path
        )
    )
    {
        return;
    }


    terminal_print(
        "No existe el directorio: "
    );

    terminal_print(path);

    terminal_putchar('\n');
}


/*
 * seedir
 */
static void command_seedir(void)
{
    filesystem_print_current_directory();
}


/*
 * see
 */
static void command_see(
    const char* path
)
{
    if (
        !filesystem_list(path)
    )
    {
        terminal_print(
            "Directorio no encontrado: "
        );

        terminal_print(path);

        terminal_putchar('\n');
    }
}


/*
 * seedir("nombre")
 */
static void command_find_directory(
    const char* command
)
{
    /*
     * Saltamos:
     *
     * seedir("
     *
     * Son 8 caracteres.
     */
    const char* text =
        command + 8;


    char name[64];

    u32 length = 0;


    /*
     * Copiar hasta encontrar ".
     */
    while (
        *text
        &&
        *text != '"'
        &&
        length < 63
    )
    {
        name[length] =
            *text;

        length++;

        text++;
    }


    name[length] = '\0';


    /*
     * Debemos tener:
     *
     * ")
     *
     * al final.
     */
    if (
        text[0] != '"'
        ||
        text[1] != ')'
        ||
        text[2] != '\0'
    )
    {
        terminal_print(
            "Uso: seedir(\"nombre\")\n"
        );

        return;
    }


    int found =
        filesystem_find_directories(
            name
        );


    if (found == 0)
    {
        terminal_print(
            "No se encontraron carpetas llamadas: "
        );

        terminal_print(name);

        terminal_putchar('\n');
    }
}


static void command_storage(void)
{
    terminal_print("Storage:\n");

    terminal_print("  ATA disk: ");

    if (rootstorage_device_available())
    {
        terminal_print("detected\n");
    }
    else
    {
        terminal_print("not detected\n");
    }

    terminal_print("  RootOS volume: ");

    if (rootstorage_volume_available())
    {
        terminal_print("ready\n");
    }
    else
    {
        terminal_print("not found\n");
    }

    terminal_print("  Filesystem backend: ");

    if (filesystem_is_persistent())
    {
        terminal_print("persistent ROOTFS42\n");
        terminal_print("  Persistent writes: enabled\n");
    }
    else
    {
        terminal_print("temporary RAM fallback\n");
        terminal_print("  Persistent writes: disabled\n");
    }

    if (filesystem_storage_faulted())
    {
        terminal_print("  Warning: storage I/O fault detected\n");
    }
}


static void shell_print_u32(
    u32 value
)
{
    char buffer[11];
    u32 length = 0u;

    if (value == 0u)
    {
        terminal_putchar('0');
        return;
    }

    while (value > 0u && length < sizeof(buffer))
    {
        buffer[length++] =
            (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (length > 0u)
    {
        terminal_putchar(buffer[--length]);
    }
}


static void shell_print_i32(
    i32 value
)
{
    if (value < 0)
    {
        terminal_putchar('-');

        /* Avoid signed overflow for INT_MIN. */
        u32 magnitude = (u32)(-(value + 1));
        magnitude += 1u;
        shell_print_u32(magnitude);
        return;
    }

    shell_print_u32((u32)value);
}


static char shell_hex_digit(u8 value)
{
    value &= 0x0Fu;

    if (value < 10u)
    {
        return (char)('0' + value);
    }

    return (char)('A' + (value - 10u));
}


static void shell_print_hex_u32_width(
    u32 value,
    u32 digits
)
{
    if (digits == 0u)
    {
        return;
    }

    if (digits > 8u)
    {
        digits = 8u;
    }

    for (u32 i = digits; i > 0u; i--)
    {
        u32 shift = (i - 1u) * 4u;
        terminal_putchar(
            shell_hex_digit(
                (u8)(value >> shift)
            )
        );
    }
}


static void shell_print_hex_u32_trimmed(u32 value)
{
    bool started = false;

    for (u32 i = 8u; i > 0u; i--)
    {
        u32 shift = (i - 1u) * 4u;
        u8 digit = (u8)((value >> shift) & 0x0Fu);

        if (!started && digit == 0u && i != 1u)
        {
            continue;
        }

        started = true;
        terminal_putchar(shell_hex_digit(digit));
    }
}


static void shell_print_hex_u64(u64 value)
{
    u32 high = (u32)(value >> 32);
    u32 low = (u32)value;

    terminal_print("0x");

    if (high != 0u)
    {
        shell_print_hex_u32_trimmed(high);
        shell_print_hex_u32_width(low, 8u);
        return;
    }

    shell_print_hex_u32_trimmed(low);
}


static void shell_print_pci_address(PciAddress address)
{
    shell_print_hex_u32_width(address.bus, 2u);
    terminal_putchar(':');
    shell_print_hex_u32_width(address.device, 2u);
    terminal_putchar('.');
    shell_print_hex_u32_width(address.function, 1u);
}


static bool shell_parse_u32_decimal(
    const char* text,
    u32* output
)
{
    if (
        text == NULL
        ||
        text[0] == '\0'
        ||
        output == NULL
    )
    {
        return false;
    }

    u32 value = 0u;

    for (usize i = 0u; text[i] != '\0'; i++)
    {
        if (text[i] < '0' || text[i] > '9')
        {
            return false;
        }

        u32 digit = (u32)(text[i] - '0');

        if (value > 429496729u)
        {
            return false;
        }

        u32 next = value * 10u + digit;

        if (next < value)
        {
            return false;
        }

        value = next;
    }

    *output = value;
    return true;
}


static void shell_print_ipv4(u32 address)
{
    shell_print_u32((address >> 24) & 0xFFu);
    terminal_putchar('.');
    shell_print_u32((address >> 16) & 0xFFu);
    terminal_putchar('.');
    shell_print_u32((address >> 8) & 0xFFu);
    terminal_putchar('.');
    shell_print_u32(address & 0xFFu);
}


static void shell_print_mac(const u8 mac[6])
{
    if (mac == NULL)
        return;

    for (u32 i = 0u; i < 6u; i++)
    {
        if (i != 0u)
            terminal_putchar(':');
        shell_print_hex_u32_width(mac[i], 2u);
    }
}


static bool shell_parse_ipv4(const char* text, u32* output)
{
    if (text == NULL || output == NULL)
        return false;

    u32 parts[4] = {0u, 0u, 0u, 0u};
    u32 part = 0u;
    bool have_digit = false;

    for (usize i = 0u;; i++)
    {
        char c = text[i];

        if (c >= '0' && c <= '9')
        {
            have_digit = true;
            parts[part] = parts[part] * 10u + (u32)(c - '0');
            if (parts[part] > 255u)
                return false;
            continue;
        }

        if (c == '.' && have_digit && part < 3u)
        {
            part++;
            have_digit = false;
            continue;
        }

        if (c == '\0' && have_digit && part == 3u)
            break;

        return false;
    }

    *output = ROOT_IPV4(parts[0], parts[1], parts[2], parts[3]);
    return true;
}


static void command_ifconfig(void)
{
    net_poll();
    const RootNetConfig* config = net_config();

    terminal_print("eth0\n");
    terminal_print("  Driver: ");
    terminal_print(net_device_active_driver());
    terminal_putchar('\n');
    terminal_print("  MAC: ");
    shell_print_mac(config->mac);
    terminal_putchar('\n');
    terminal_print("  Link: ");
    terminal_print(config->link_up ? "up\n" : "down\n");

    terminal_print("  IPv4: ");
    if (config->configured)
        shell_print_ipv4(config->ipv4_address);
    else
        terminal_print("not configured");
    terminal_putchar('\n');

    terminal_print("  Netmask: ");
    shell_print_ipv4(config->subnet_mask);
    terminal_putchar('\n');
    terminal_print("  Gateway: ");
    shell_print_ipv4(config->gateway);
    terminal_putchar('\n');
    terminal_print("  DNS: ");
    shell_print_ipv4(config->dns_server);
    terminal_putchar('\n');

    if (config->dhcp)
    {
        terminal_print("  DHCP server: ");
        shell_print_ipv4(config->dhcp_server);
        terminal_putchar('\n');
        terminal_print("  Lease: ");
        shell_print_u32(config->lease_seconds);
        terminal_print(" seconds\n");
    }
}


static void command_net(void)
{
    net_poll();
    const RootNetConfig* config = net_config();

    terminal_print("Network:\n");
    terminal_print("  Adapter: ");
    if (config->adapter_ready)
    {
        terminal_print(net_device_active_driver());
        terminal_print(" ready\n");
    }
    else
    {
        terminal_print("not detected/failed\n");
    }
    terminal_print("  Link: ");
    terminal_print(config->link_up ? "up\n" : "down\n");
    terminal_print("  MAC: ");
    shell_print_mac(config->mac);
    terminal_putchar('\n');
    terminal_print("  DHCP: ");
    terminal_print(dhcp_state_name(dhcp_state()));
    terminal_putchar('\n');

    if (!config->adapter_ready)
    {
        terminal_print("  Driver Store: ");
        terminal_print(driver_store_last_error());
        terminal_putchar('\n');
        terminal_print("  Check 'pci', 'devices' and 'driverpack'.\n");
    }

    if (config->configured)
    {
        terminal_print("  IPv4: ");
        shell_print_ipv4(config->ipv4_address);
        terminal_putchar('\n');
    }
}


static void command_net_devices(void)
{
    usize count = net_device_count();
    terminal_print("Network devices: ");
    shell_print_u32((u32)count);
    terminal_putchar('\n');

    if (count == 0u)
    {
        terminal_print("No network adapter registered.\n");
        return;
    }

    terminal_print("ID ACTIVE DRIVER     READY LINK MAC\n");

    for (usize i = 0u; i < count; i++)
    {
        RootNetDevice device;
        if (!net_device_get(i, &device))
            continue;

        bool ready =
            device.ops.ready != NULL
            && device.ops.ready(device.context);
        bool link =
            device.ops.link_up != NULL
            && device.ops.link_up(device.context);

        shell_print_u32((u32)i);
        terminal_print(net_device_is_active(i) ? "  *     " : "        ");
        terminal_print(device.driver_name != NULL ? device.driver_name : "unknown");
        terminal_print("  ");
        terminal_print(ready ? "yes   " : "no    ");
        terminal_print(link ? "up   " : "down ");
        shell_print_mac(device.mac);
        terminal_putchar('\n');
    }
}

static void command_net_use(const char* arguments)
{
    u32 id = 0u;
    if (
        !shell_parse_u32_decimal(arguments, &id)
        || id >= net_device_count()
    )
    {
        terminal_print("Usage: net use <id>\n");
        command_net_devices();
        return;
    }

    if (!net_select_device((usize)id))
    {
        terminal_print("net: could not select adapter\n");
        return;
    }

    terminal_print("Selected network device ");
    shell_print_u32(id);
    terminal_print(" (");
    terminal_print(net_device_active_driver());
    terminal_print("). Run 'dhcp'.\n");
}

static void command_dhcp(void)
{
    terminal_print("DHCP: requesting address...\n");

    if (!dhcp_acquire())
    {
        terminal_print("DHCP failed: ");
        terminal_print(dhcp_last_error());
        terminal_putchar('\n');
        return;
    }

    terminal_print("DHCP bound.\n");
    command_ifconfig();
}


static void command_arp(const char* arguments)
{
    while (arguments != NULL && *arguments == ' ')
        arguments++;

    if (arguments != NULL && *arguments != '\0')
    {
        u32 address = 0u;
        if (!shell_parse_ipv4(arguments, &address))
        {
            terminal_print("Usage: arp [a.b.c.d]\n");
            return;
        }

        if (!arp_request(address))
        {
            terminal_print("ARP request failed. Configure IPv4 first with 'dhcp'.\n");
            return;
        }

        u64 deadline = root_time_millis() + 500u;
        while (root_time_millis() < deadline)
        {
            net_poll();
            u8 mac[6];
            if (arp_lookup(address, mac))
                break;
            __asm__ volatile ("hlt");
        }
    }
    else
    {
        net_poll();
    }

    usize count = arp_entry_count();
    terminal_print("ARP entries: ");
    shell_print_u32((u32)count);
    terminal_putchar('\n');

    for (usize i = 0u; i < count; i++)
    {
        ArpEntry entry;
        if (!arp_get_entry(i, &entry))
            continue;

        terminal_print("  ");
        shell_print_ipv4(entry.ipv4);
        terminal_print("  ");
        shell_print_mac(entry.mac);
        terminal_putchar('\n');
    }
}


static void command_dns(const char* arguments)
{
    const char* cursor = arguments;
    char target[ROOT_DNS_NAME_MAX + 1u];

    if (!shell_read_argument(&cursor, target, sizeof(target)))
    {
        terminal_print("Usage: dns <name> | dns cache | dns flush\n");
        return;
    }

    if (*cursor != '\0')
    {
        terminal_print("Usage: dns <name> | dns cache | dns flush\n");
        return;
    }

    if (root_streq(target, "flush"))
    {
        dns_cache_flush();
        terminal_print("DNS cache cleared.\n");
        return;
    }

    if (root_streq(target, "cache"))
    {
        usize count = dns_cache_count();
        terminal_print("DNS cache entries: ");
        shell_print_u32((u32)count);
        terminal_putchar('\n');

        for (usize i = 0u; i < count; i++)
        {
            DnsCacheEntry entry;
            if (!dns_cache_get(i, &entry))
                continue;

            terminal_print("  ");
            terminal_print(entry.name);
            terminal_print("  ");
            shell_print_ipv4(entry.address);
            terminal_putchar('\n');
        }
        return;
    }

    terminal_print("DNS: resolving ");
    terminal_print(target);
    terminal_print("...\n");

    u32 address = 0u;
    u32 ttl = 0u;
    DnsResult result = dns_resolve_ipv4(target, &address, &ttl);
    if (result != DNS_RESULT_OK)
    {
        terminal_print("DNS failed: ");
        terminal_print(dns_result_name(result));
        terminal_putchar('\n');
        return;
    }

    terminal_print(target);
    terminal_print(" -> ");
    shell_print_ipv4(address);
    terminal_print("  TTL=");
    shell_print_u32(ttl);
    terminal_print("s\n");
}


static bool shell_resolve_host(const char* text, u32* address)
{
    if (text == NULL || address == NULL)
        return false;

    if (shell_parse_ipv4(text, address))
        return true;

    return dns_resolve_ipv4(text, address, NULL) == DNS_RESULT_OK;
}


static void command_tcp(const char* arguments)
{
    const char* cursor = arguments;
    char action[32];

    if (!shell_read_argument(&cursor, action, sizeof(action)))
    {
        terminal_print(
            "Usage:\n"
            "  tcp connect <host|ipv4> <port>\n"
            "  tcp list\n"
            "  tcp close <id>\n"
        );
        return;
    }

    if (root_streq(action, "list"))
    {
        if (*cursor != '\0')
        {
            terminal_print("Usage: tcp list\n");
            return;
        }

        usize count = tcp_connection_count();
        terminal_print("TCP connections: ");
        shell_print_u32((u32)count);
        terminal_putchar('\n');

        for (usize i = 0u; i < count; i++)
        {
            RootTcpConnection connection;
            int id = -1;
            if (!tcp_get_connection(i, &connection, &id))
                continue;

            terminal_print("  id=");
            shell_print_u32((u32)id);
            terminal_print("  ");
            shell_print_ipv4(connection.remote_ip);
            terminal_putchar(':');
            shell_print_u32(connection.remote_port);
            terminal_print("  ");
            terminal_print(tcp_state_name(connection.state));
            terminal_putchar('\n');
        }
        return;
    }

    if (root_streq(action, "close"))
    {
        char id_text[32];
        u32 id = 0u;
        if (
            !shell_read_argument(&cursor, id_text, sizeof(id_text)) ||
            *cursor != '\0' ||
            !shell_parse_u32_decimal(id_text, &id) ||
            !tcp_close((int)id)
        )
        {
            terminal_print("Usage: tcp close <id>\n");
            return;
        }

        terminal_print("TCP connection closed.\n");
        return;
    }

    if (root_streq(action, "connect"))
    {
        char host[ROOT_DNS_NAME_MAX + 1u];
        char port_text[32];
        u32 port = 0u;
        u32 address = 0u;

        if (
            !shell_read_argument(&cursor, host, sizeof(host)) ||
            !shell_read_argument(&cursor, port_text, sizeof(port_text)) ||
            *cursor != '\0' ||
            !shell_parse_u32_decimal(port_text, &port) ||
            port == 0u ||
            port > 65535u
        )
        {
            terminal_print("Usage: tcp connect <host|ipv4> <port>\n");
            return;
        }

        terminal_print("TCP: resolving/connecting...\n");
        if (!shell_resolve_host(host, &address))
        {
            terminal_print("TCP failed: could not resolve host.\n");
            return;
        }

        int id = tcp_connect(address, (u16)port, 5000u);
        if (id < 0)
        {
            terminal_print("TCP failed: no connection slot or network unavailable.\n");
            return;
        }

        RootTcpState state = tcp_state(id);
        terminal_print("TCP id=");
        shell_print_u32((u32)id);
        terminal_print("  ");
        shell_print_ipv4(address);
        terminal_putchar(':');
        shell_print_u32(port);
        terminal_print("  ");
        terminal_print(tcp_state_name(state));
        terminal_putchar('\n');

        if (state != ROOT_TCP_ESTABLISHED)
        {
            terminal_print("  Error: ");
            terminal_print(tcp_last_error(id));
            terminal_putchar('\n');
        }
        return;
    }

    terminal_print(
        "Usage:\n"
        "  tcp connect <host|ipv4> <port>\n"
        "  tcp list\n"
        "  tcp close <id>\n"
    );
}


static void command_pci(const char* arguments)
{
    const char* cursor = arguments;
    char action[32];

    if (
        shell_read_argument(
            &cursor,
            action,
            sizeof(action)
        )
    )
    {
        if (!root_streq(action, "rescan") || *cursor != '\0')
        {
            terminal_print("Usage: pci [rescan]\n");
            return;
        }

        device_manager_rescan();
        terminal_print("PCI rescan complete: ");
        shell_print_u32((u32)pci_device_count());
        terminal_print(" device(s).\n");
        return;
    }

    usize count = pci_device_count();

    terminal_print("PCI devices: ");
    shell_print_u32((u32)count);
    terminal_putchar('\n');

    if (count == 0u)
    {
        terminal_print("No PCI devices detected.\n");
        return;
    }

    terminal_print("BDF       VID:DID   CLASS     IRQ  DEVICE\n");

    for (usize i = 0u; i < count; i++)
    {
        PciDevice device;

        if (!pci_get_device(i, &device))
        {
            continue;
        }

        shell_print_pci_address(device.address);
        terminal_print("  ");
        shell_print_hex_u32_width(device.vendor_id, 4u);
        terminal_putchar(':');
        shell_print_hex_u32_width(device.device_id, 4u);
        terminal_print("  ");
        shell_print_hex_u32_width(device.class_code, 2u);
        terminal_putchar(':');
        shell_print_hex_u32_width(device.subclass, 2u);
        terminal_putchar(':');
        shell_print_hex_u32_width(device.prog_if, 2u);
        terminal_print("  ");
        shell_print_u32(device.interrupt_line);
        terminal_print("  ");
        terminal_print(
            pci_subclass_name(
                device.class_code,
                device.subclass,
                device.prog_if
            )
        );
        terminal_putchar('\n');
    }
}


static void command_devices(void)
{
    usize count = device_manager_count();

    terminal_print("Devices: ");
    shell_print_u32((u32)count);
    terminal_putchar('\n');

    if (count == 0u)
    {
        terminal_print("Device Manager inventory is empty.\n");
        return;
    }

    terminal_print("ID  BUS  BDF       STATE    DEVICE / DRIVER\n");

    for (usize i = 0u; i < count; i++)
    {
        RootDevice device;

        if (!device_manager_get(i, &device))
        {
            continue;
        }

        shell_print_u32(device.id);
        terminal_print("  ");
        terminal_print(device_manager_bus_name(device.bus));
        terminal_print("  ");

        if (device.bus == ROOT_DEVICE_BUS_PCI)
        {
            shell_print_pci_address(device.pci.address);
        }
        else
        {
            terminal_print("--:--.-");
        }

        terminal_print("  ");
        terminal_print(
            device_manager_driver_state_name(
                device.driver_state
            )
        );
        terminal_print("  ");
        terminal_print(
            device.display_name != NULL
            ?
            device.display_name
            :
            "Unknown device"
        );

        if (device.driver_name != NULL)
        {
            terminal_print(" / ");
            terminal_print(device.driver_name);
        }

        terminal_putchar('\n');
    }
}


static void command_device(const char* arguments)
{
    const char* cursor = arguments;
    char action[32];
    char id_text[32];

    if (
        !shell_read_argument(
            &cursor,
            action,
            sizeof(action)
        )
        ||
        !root_streq(action, "info")
        ||
        !shell_read_argument(
            &cursor,
            id_text,
            sizeof(id_text)
        )
        ||
        *cursor != '\0'
    )
    {
        terminal_print("Usage: device info <id>\n");
        return;
    }

    u32 id = 0u;

    if (!shell_parse_u32_decimal(id_text, &id))
    {
        terminal_print("device: invalid id\n");
        return;
    }

    RootDevice device;

    if (!device_manager_get((usize)id, &device))
    {
        terminal_print("device: id not found\n");
        return;
    }

    terminal_print("Device ");
    shell_print_u32(device.id);
    terminal_putchar('\n');

    terminal_print("  Name: ");
    terminal_print(
        device.display_name != NULL
        ?
        device.display_name
        :
        "Unknown"
    );
    terminal_putchar('\n');

    terminal_print("  Bus: ");
    terminal_print(device_manager_bus_name(device.bus));
    terminal_putchar('\n');

    terminal_print("  Driver: ");
    terminal_print(
        device.driver_name != NULL
        ?
        device.driver_name
        :
        "none"
    );
    terminal_print(" (");
    terminal_print(
        device_manager_driver_state_name(
            device.driver_state
        )
    );
    terminal_print(")\n");

    if (device.bus != ROOT_DEVICE_BUS_PCI)
    {
        return;
    }

    const PciDevice* pci = &device.pci;

    terminal_print("  PCI address: ");
    shell_print_pci_address(pci->address);
    terminal_putchar('\n');

    terminal_print("  Vendor/device: ");
    shell_print_hex_u32_width(pci->vendor_id, 4u);
    terminal_putchar(':');
    shell_print_hex_u32_width(pci->device_id, 4u);
    terminal_putchar('\n');

    terminal_print("  Class: ");
    shell_print_hex_u32_width(pci->class_code, 2u);
    terminal_putchar(':');
    shell_print_hex_u32_width(pci->subclass, 2u);
    terminal_putchar(':');
    shell_print_hex_u32_width(pci->prog_if, 2u);
    terminal_print("  ");
    terminal_print(
        pci_subclass_name(
            pci->class_code,
            pci->subclass,
            pci->prog_if
        )
    );
    terminal_putchar('\n');

    terminal_print("  Revision: 0x");
    shell_print_hex_u32_width(pci->revision_id, 2u);
    terminal_putchar('\n');

    terminal_print("  Command/status: 0x");
    shell_print_hex_u32_width(pci->command, 4u);
    terminal_print(" / 0x");
    shell_print_hex_u32_width(pci->status, 4u);
    terminal_putchar('\n');

    terminal_print("  IRQ line/pin: ");
    shell_print_u32(pci->interrupt_line);
    terminal_print(" / ");
    shell_print_u32(pci->interrupt_pin);
    terminal_putchar('\n');

    bool found_bar = false;

    for (u8 i = 0u; i < pci->bar_count && i < PCI_MAX_BARS; i++)
    {
        const PciBar* bar = &pci->bars[i];

        if (!bar->present)
        {
            continue;
        }

        found_bar = true;
        terminal_print("  BAR");
        shell_print_u32(i);
        terminal_print(": ");
        terminal_print(pci_bar_type_name(bar->type));
        terminal_putchar(' ');
        shell_print_hex_u64(bar->base);

        if (bar->prefetchable)
        {
            terminal_print(" prefetchable");
        }

        terminal_putchar('\n');
    }

    if (!found_bar)
    {
        terminal_print("  BARs: none\n");
    }
}


static void command_drivers(void)
{
    usize count = device_manager_driver_count();

    terminal_print("Registered drivers: ");
    shell_print_u32((u32)count);
    terminal_putchar('\n');

    if (count == 0u)
    {
        terminal_print(
            "No Device Manager drivers registered.\n"
        );
        return;
    }

    for (usize i = 0u; i < count; i++)
    {
        const RootDriver* driver = NULL;

        if (!device_manager_get_driver(i, &driver) || driver == NULL)
        {
            continue;
        }

        terminal_print("  ");
        shell_print_u32((u32)i);
        terminal_print(": ");
        terminal_print(driver->name);
        terminal_print("  match=");

        if (driver->match.vendor_id == ROOT_DRIVER_ANY_VENDOR)
        {
            terminal_putchar('*');
        }
        else
        {
            shell_print_hex_u32_width(driver->match.vendor_id, 4u);
        }

        terminal_putchar(':');

        if (driver->match.device_id == ROOT_DRIVER_ANY_DEVICE)
        {
            terminal_putchar('*');
        }
        else
        {
            shell_print_hex_u32_width(driver->match.device_id, 4u);
        }

        terminal_print(" class=");

        if (driver->match.class_code == ROOT_DRIVER_ANY_CLASS)
        {
            terminal_putchar('*');
        }
        else
        {
            shell_print_hex_u32_width(driver->match.class_code, 2u);
        }

        terminal_putchar(':');

        if (driver->match.subclass == ROOT_DRIVER_ANY_CLASS)
        {
            terminal_putchar('*');
        }
        else
        {
            shell_print_hex_u32_width(driver->match.subclass, 2u);
        }

        terminal_putchar(':');

        if (driver->match.prog_if == ROOT_DRIVER_ANY_CLASS)
        {
            terminal_putchar('*');
        }
        else
        {
            shell_print_hex_u32_width(driver->match.prog_if, 2u);
        }

        terminal_putchar('\n');
    }

    terminal_print("Driver Pack: ");
    if (!driver_store_available())
    {
        terminal_print("unavailable (");
        terminal_print(driver_store_last_error());
        terminal_print(")\n");
    }
    else
    {
        terminal_print("available, loaded=");
        shell_print_u32((u32)driver_store_loaded_count());
        terminal_putchar('\n');
    }

}


static void shell_print_bcd_version(u16 value)
{
    u8 high = (u8)((value >> 8) & 0xFFu);
    u8 low = (u8)(value & 0xFFu);

    shell_print_hex_u32_width((high >> 4) & 0x0Fu, 1u);
    shell_print_hex_u32_width(high & 0x0Fu, 1u);
    terminal_putchar('.');
    shell_print_hex_u32_width((low >> 4) & 0x0Fu, 1u);
    shell_print_hex_u32_width(low & 0x0Fu, 1u);
}


static void command_usb_list(void)
{
    if (
        xhci_controller_count() > 0u
        &&
        !xhci_any_running()
    )
    {
        terminal_print("USB: initializing xHCI...\n");

        if (!xhci_start())
        {
            terminal_print(
                "USB: initialization failed. Use 'usb controllers'.\n"
            );
        }
        else
        {
            (void)rndis_probe_all();
        }
    }

    usize count = usb_device_count();

    terminal_print("USB devices: ");
    shell_print_u32((u32)count);
    terminal_putchar('\n');

    if (count == 0u)
    {
        if (xhci_controller_count() == 0u)
        {
            terminal_print(
                "No xHCI controller detected. Use 'pci' to check for 0C:03:30.\n"
            );
        }
        else
        {
            terminal_print(
                "No directly connected USB device was enumerated.\n"
                "Use 'usb ports' and 'usb rescan' after connecting a device.\n"
            );
        }

        return;
    }

    terminal_print(
        "ID  CTRL PORT SLOT SPEED        VID:PID   CLASS         PRODUCT\n"
    );

    for (usize i = 0u; i < count; i++)
    {
        UsbDeviceInfo device;

        if (!usb_get_device(i, &device))
        {
            continue;
        }

        shell_print_u32(device.id);
        terminal_print("  ");
        shell_print_u32(device.controller_index);
        terminal_print("    ");
        shell_print_u32(device.port_number);
        terminal_print("    ");
        shell_print_u32(device.slot_id);
        terminal_print("    ");

        switch (device.speed)
        {
            case USB_SPEED_LOW: terminal_print("Low         "); break;
            case USB_SPEED_FULL: terminal_print("Full        "); break;
            case USB_SPEED_HIGH: terminal_print("High        "); break;
            case USB_SPEED_SUPER: terminal_print("Super       "); break;
            case USB_SPEED_SUPER_PLUS: terminal_print("Super+      "); break;
            default: terminal_print("Unknown     "); break;
        }

        shell_print_hex_u32_width(device.vendor_id, 4u);
        terminal_putchar(':');
        shell_print_hex_u32_width(device.product_id, 4u);
        terminal_print("  ");

        u8 display_class = device.device_class;

        if (
            display_class == 0u
            &&
            device.interface_count > 0u
        )
        {
            display_class = device.interfaces[0].class_code;
        }

        terminal_print(usb_class_name(display_class));
        terminal_print("  ");

        if (device.product[0] != '\0')
        {
            terminal_print(device.product);
        }
        else if (device.manufacturer[0] != '\0')
        {
            terminal_print(device.manufacturer);
        }
        else
        {
            terminal_print("(no product string)");
        }

        terminal_putchar('\n');
    }
}


static void command_usb_controllers(void)
{
    usize count = xhci_controller_count();

    terminal_print("xHCI controllers: ");
    shell_print_u32((u32)count);
    terminal_putchar('\n');

    if (count == 0u)
    {
        terminal_print("No xHCI controller detected by PCI/Device Manager.\n");
        return;
    }

    for (usize i = 0u; i < count; i++)
    {
        XhciControllerInfo controller;

        if (!xhci_get_controller_info(i, &controller))
        {
            continue;
        }

        terminal_print("Controller ");
        shell_print_u32(controller.index);
        terminal_putchar('\n');

        terminal_print("  PCI: ");
        shell_print_pci_address(controller.pci_address);
        terminal_putchar('\n');

        terminal_print("  MMIO: ");
        shell_print_hex_u64(controller.mmio_base);
        terminal_putchar('\n');

        terminal_print("  State: ");

        if (controller.active)
        {
            terminal_print("running");
        }
        else if (controller.init_failed)
        {
            terminal_print("initialization failed");
        }
        else if (controller.init_attempted)
        {
            terminal_print("stopped");
        }
        else
        {
            terminal_print("detected (not initialized yet)");
        }

        terminal_putchar('\n');

        if (controller.init_failed)
        {
            terminal_print("  Last error: ");
            terminal_print(xhci_error_name(controller.last_error));
            terminal_putchar('\n');
        }

        if (!controller.init_attempted && !controller.active)
        {
            terminal_print("  Run 'usb start' to initialize hardware.\n");
            continue;
        }

        terminal_print("  xHCI version: ");
        shell_print_bcd_version(controller.hci_version);
        terminal_putchar('\n');

        terminal_print("  Slots: ");
        shell_print_u32(controller.enabled_slots);
        terminal_print(" enabled / ");
        shell_print_u32(controller.max_slots);
        terminal_print(" hardware\n");

        terminal_print("  Root ports: ");
        shell_print_u32(controller.max_ports);
        terminal_putchar('\n');

        terminal_print("  Context size: ");
        shell_print_u32(controller.context_size);
        terminal_print(" bytes\n");

        terminal_print("  Scratchpads: ");
        shell_print_u32(controller.scratchpad_count);
        terminal_putchar('\n');

        terminal_print("  Enumerated devices: ");
        shell_print_u32((u32)controller.enumerated_devices);
        terminal_putchar('\n');
    }
}


static void command_usb_health(void)
{
    usize count = xhci_controller_count();

    terminal_print("USB host core: xHCI modern host-controller backend\n");
    terminal_print("Class drivers: descriptors/control, Mass Storage BOT, RNDIS\n");
    terminal_print("Recovery: hotplug + endpoint STALL reset + circular TRB rings\n");

    if (count == 0u)
    {
        terminal_print("No xHCI controller detected.\n");
        return;
    }

    for (usize i = 0u; i < count; i++)
    {
        XhciControllerInfo controller;
        if (!xhci_get_controller_info(i, &controller))
            continue;

        terminal_print("Controller ");
        shell_print_u32(controller.index);
        terminal_print(": hotplug=");
        shell_print_u32(controller.hotplug_events);
        terminal_print(" disconnects=");
        shell_print_u32(controller.disconnect_events);
        terminal_print(" endpoint-recoveries=");
        shell_print_u32(controller.endpoint_recoveries);
        terminal_print(" transfer-errors=");
        shell_print_u32(controller.transfer_errors);
        terminal_putchar('\n');
    }
}


static void command_usb_ports(void)
{
    if (
        xhci_controller_count() > 0u
        &&
        !xhci_any_running()
    )
    {
        terminal_print("USB: controller detected but not running; use 'usb start'.\n");
    }

    usize count = xhci_port_count();

    terminal_print("xHCI root ports: ");
    shell_print_u32((u32)count);
    terminal_putchar('\n');

    if (count == 0u)
    {
        terminal_print("No xHCI ports available.\n");
        return;
    }

    terminal_print(
        "CTRL PORT PROTO CONNECT ENABLE POWER SPEED LINK      USB\n"
    );

    for (usize i = 0u; i < count; i++)
    {
        XhciPortInfo port;

        if (!xhci_get_port_info(i, &port))
        {
            continue;
        }

        shell_print_u32(port.controller_index);
        terminal_print("    ");
        shell_print_u32(port.port_number);
        terminal_print("    ");
        terminal_print(xhci_protocol_name(port.protocol_major));
        terminal_print("  ");
        terminal_print(port.connected ? "yes     " : "no      ");
        terminal_print(port.enabled ? "yes    " : "no     ");
        terminal_print(port.powered ? "on    " : "off   ");
        shell_print_u32(port.speed_id);
        terminal_print("     ");
        terminal_print(xhci_link_state_name(port.link_state));
        terminal_print("  ");

        if (port.has_usb_device)
        {
            shell_print_u32(port.usb_device_id);
        }
        else
        {
            terminal_putchar('-');
        }

        terminal_putchar('\n');
    }
}


static void command_usb_info(const char* id_text)
{
    u32 id = 0u;

    if (!shell_parse_u32_decimal(id_text, &id))
    {
        terminal_print("usb: invalid id\n");
        return;
    }

    UsbDeviceInfo device;

    if (!usb_get_device_by_id(id, &device))
    {
        terminal_print("usb: device id not found\n");
        return;
    }

    terminal_print("USB device ");
    shell_print_u32(device.id);
    terminal_putchar('\n');

    terminal_print("  Controller/port: ");
    shell_print_u32(device.controller_index);
    terminal_putchar('/');
    shell_print_u32(device.port_number);
    terminal_putchar('\n');

    terminal_print("  Slot/address: ");
    shell_print_u32(device.slot_id);
    terminal_putchar('/');
    shell_print_u32(device.address);
    terminal_putchar('\n');

    terminal_print("  Speed: ");
    terminal_print(usb_speed_name(device.speed));
    terminal_print(" (xHCI speed ID ");
    shell_print_u32(device.speed_id);
    terminal_print(")\n");

    terminal_print("  Vendor/Product ID: ");
    shell_print_hex_u32_width(device.vendor_id, 4u);
    terminal_putchar(':');
    shell_print_hex_u32_width(device.product_id, 4u);
    terminal_putchar('\n');

    terminal_print("  Manufacturer: ");
    terminal_print(
        device.manufacturer[0] != '\0'
        ? device.manufacturer
        : "(not provided)"
    );
    terminal_putchar('\n');

    terminal_print("  Product: ");
    terminal_print(
        device.product[0] != '\0'
        ? device.product
        : "(not provided)"
    );
    terminal_putchar('\n');

    terminal_print("  Serial: ");
    terminal_print(
        device.serial[0] != '\0'
        ? device.serial
        : "(not provided)"
    );
    terminal_putchar('\n');

    terminal_print("  USB version: ");
    shell_print_bcd_version(device.usb_version_bcd);
    terminal_putchar('\n');

    terminal_print("  Device version: ");
    shell_print_bcd_version(device.device_version_bcd);
    terminal_putchar('\n');

    terminal_print("  Device class: 0x");
    shell_print_hex_u32_width(device.device_class, 2u);
    terminal_putchar(':');
    shell_print_hex_u32_width(device.device_subclass, 2u);
    terminal_putchar(':');
    shell_print_hex_u32_width(device.device_protocol, 2u);
    terminal_print("  ");
    terminal_print(usb_class_name(device.device_class));
    terminal_putchar('\n');

    terminal_print("  Endpoint 0 max packet: ");
    shell_print_u32(device.endpoint0_max_packet);
    terminal_print(" bytes\n");

    terminal_print("  Configurations: ");
    shell_print_u32(device.configuration_count);
    terminal_putchar('\n');

    if (device.configuration_count > 0u)
    {
        terminal_print("  First configuration value: ");
        shell_print_u32(device.configuration_value);
        terminal_putchar('\n');

        terminal_print("  Configuration attributes: 0x");
        shell_print_hex_u32_width(device.configuration_attributes, 2u);
        terminal_putchar('\n');

        terminal_print("  Maximum bus power: ");
        shell_print_u32(device.max_power_ma);
        terminal_print(" mA\n");
    }

    terminal_print("  Interfaces parsed: ");
    shell_print_u32(device.interface_count);
    terminal_putchar('\n');

    for (u8 i = 0u; i < device.interface_count; i++)
    {
        const UsbInterfaceInfo* interface = &device.interfaces[i];

        terminal_print("    Interface ");
        shell_print_u32(interface->number);
        terminal_print(" alt ");
        shell_print_u32(interface->alternate_setting);
        terminal_print(": class 0x");
        shell_print_hex_u32_width(interface->class_code, 2u);
        terminal_putchar(':');
        shell_print_hex_u32_width(interface->subclass, 2u);
        terminal_putchar(':');
        shell_print_hex_u32_width(interface->protocol, 2u);
        terminal_print("  ");
        terminal_print(usb_class_name(interface->class_code));
        terminal_print("  endpoints=");
        shell_print_u32(interface->endpoint_count);
        terminal_putchar('\n');

        for (u8 e = 0u; e < interface->endpoint_count; e++)
        {
            const UsbEndpointInfo* endpoint = &interface->endpoints[e];

            terminal_print("      EP 0x");
            shell_print_hex_u32_width(endpoint->address, 2u);
            terminal_putchar(' ');
            terminal_print(usb_direction_name(endpoint->address));
            terminal_putchar(' ');
            terminal_print(usb_transfer_type_name(endpoint->attributes));
            terminal_print(" maxpacket=");
            shell_print_u32(endpoint->max_packet_size);
            terminal_print(" interval=");
            shell_print_u32(endpoint->interval);
            terminal_putchar('\n');
        }
    }
}


static void command_rndis(void)
{
    bool initialized = rndis_probe_all();
    usize count = rndis_device_count();

    terminal_print("RNDIS devices: ");
    shell_print_u32((u32)count);
    terminal_putchar('\n');

    if (count == 0u)
    {
        terminal_print("RNDIS: ");
        terminal_print(rndis_last_error());
        terminal_putchar('\n');
        terminal_print("Enable USB tethering, then run 'usb rescan' and 'rndis'.\n");
        return;
    }

    for (usize i = 0u; i < count; i++)
    {
        RndisDeviceInfo info;
        if (!rndis_get_device(i, &info))
            continue;

        terminal_print("  USB ");
        shell_print_u32(info.usb_device_id);
        terminal_print(" ");
        shell_print_hex_u32_width(info.vendor_id, 4u);
        terminal_putchar(':');
        shell_print_hex_u32_width(info.product_id, 4u);
        terminal_print("  ");
        terminal_print(info.ready ? "ready" : "failed");
        terminal_print(" link=");
        terminal_print(info.link_up ? "up" : "down");
        terminal_print(" MAC=");
        shell_print_mac(info.mac);
        terminal_putchar('\n');

        terminal_print("    control-if=");
        shell_print_u32(info.control_interface);
        terminal_print(" data-if=");
        shell_print_u32(info.data_interface);
        terminal_print(" bulk-out=0x");
        shell_print_hex_u32_width(info.bulk_out_endpoint, 2u);
        terminal_print(" bulk-in=0x");
        shell_print_hex_u32_width(info.bulk_in_endpoint, 2u);
        terminal_putchar('\n');

        if (!info.ready && info.last_error != NULL)
        {
            terminal_print("    error: ");
            terminal_print(info.last_error);
            terminal_putchar('\n');
        }
    }

    if (!initialized)
    {
        terminal_print("Last RNDIS error: ");
        terminal_print(rndis_last_error());
        terminal_putchar('\n');
    }
}

static void command_usb(const char* arguments)
{
    const char* cursor = arguments;
    char action[32];

    if (!shell_read_argument(&cursor, action, sizeof(action)))
    {
        command_usb_list();
        return;
    }

    if (root_streq(action, "controllers") && *cursor == '\0')
    {
        command_usb_controllers();
        return;
    }

    if (root_streq(action, "ports") && *cursor == '\0')
    {
        command_usb_ports();
        return;
    }

    if (root_streq(action, "health") && *cursor == '\0')
    {
        command_usb_health();
        return;
    }

    if (root_streq(action, "start") && *cursor == '\0')
    {
        bool ok = xhci_start();

        terminal_print(
            ok
            ? "USB xHCI initialization complete.\n"
            : "USB xHCI initialization failed; see 'usb controllers'.\n"
        );

        if (ok)
        {
            usb_service();
            rndis_service();
            (void)rndis_probe_all();
            command_usb_list();
        }
        else
        {
            command_usb_controllers();
        }

        return;
    }

    if (root_streq(action, "rescan") && *cursor == '\0')
    {
        rndis_invalidate_all();
        bool ok = xhci_rescan();

        terminal_print(
            ok
            ? "USB rescan complete.\n"
            : "USB rescan completed with controller errors.\n"
        );

        if (ok)
        {
            usb_service();
            rndis_service();
            (void)rndis_probe_all();
            command_usb_list();
        }
        else
        {
            command_usb_controllers();
        }

        return;
    }

    if (root_streq(action, "info"))
    {
        char id_text[32];

        if (
            !shell_read_argument(
                &cursor,
                id_text,
                sizeof(id_text)
            )
            ||
            *cursor != '\0'
        )
        {
            terminal_print("Usage: usb info <id>\n");
            return;
        }

        command_usb_info(id_text);
        return;
    }

    if (root_streq(arguments, "storage"))
    {
        bool found = usb_mass_storage_scan();
        usize count = usb_mass_storage_count();

        terminal_print("USB Mass Storage: ");
        shell_print_u32((u32)count);
        terminal_putchar('\n');

        if (!found && count == 0u)
        {
            terminal_print("No usable Bulk-Only mass-storage device found.\n");
            return;
        }

        for (usize i = 0u; i < count; i++)
        {
            UsbMassStorageInfo info;

            if (!usb_mass_storage_get(i, &info))
            {
                continue;
            }

            terminal_print("  USB ");
            shell_print_u32(info.usb_device_id);
            terminal_print(" -> disk ");
            shell_print_u32(info.block_device_id);
            terminal_print("  ");
            terminal_print(info.product[0] != '\0' ? info.product : "Mass Storage");
            terminal_print("  block=");
            shell_print_u32(info.block_size);
            terminal_print(" bytes\n");
        }

        return;
    }

    terminal_print(
        "Usage: usb [start|controllers|ports|rescan|storage|info <id>]\n"
    );
}


static void command_ps(void)
{
    usize count = process_count();

    if (count == 0u)
    {
        terminal_print("No processes have been started yet.\n");
        return;
    }

    terminal_print("PID  STATE   EXIT  NAME\n");

    for (usize i = 0; i < count; i++)
    {
        RootProcessInfo process;

        if (!process_get(i, &process))
        {
            continue;
        }

        shell_print_u32(process.pid);
        terminal_print("  ");
        terminal_print(process_state_name(process.state));
        terminal_print("  ");
        shell_print_i32(process.exit_code);
        terminal_print("  ");
        terminal_print(process.name);
        terminal_putchar('\n');
    }
}


static void command_disks(void)
{
    usize count = block_device_count();
    terminal_print("Block devices: ");
    shell_print_u32((u32)count);
    terminal_putchar('\n');

    for (usize i = 0u; i < count; i++)
    {
        RootBlockDevice device;
        if (!block_device_get(i, &device)) continue;

        terminal_print("  disk ");
        shell_print_u32(device.id);
        terminal_print("  ");
        terminal_print(block_device_bus_name(device.bus));
        terminal_print("  ");
        terminal_print(device.model);
        terminal_print("  block=");
        shell_print_u32(device.block_size);
        terminal_print(" count=");
        shell_print_hex_u64(device.block_count);
        terminal_print(device.read_only ? "  RO\n" : "  RW\n");
    }

    if (count == 0u)
    {
        terminal_print("Run 'usb storage' after USB enumeration.\n");
    }
}

static void command_disk_info(const char* text)
{
    u32 id = 0u;
    if (!shell_parse_u32_decimal(text, &id))
    {
        terminal_print("disk: invalid id\n");
        return;
    }

    RootBlockDevice device;
    if (!block_device_get_by_id(id, &device))
    {
        terminal_print("disk: device not found\n");
        return;
    }

    terminal_print("Disk "); shell_print_u32(device.id); terminal_putchar('\n');
    terminal_print("  Bus: "); terminal_print(block_device_bus_name(device.bus)); terminal_putchar('\n');
    terminal_print("  Name: "); terminal_print(device.name); terminal_putchar('\n');
    terminal_print("  Model: "); terminal_print(device.model); terminal_putchar('\n');
    terminal_print("  Block size: "); shell_print_u32(device.block_size); terminal_print(" bytes\n");
    terminal_print("  Block count: "); shell_print_hex_u64(device.block_count); terminal_putchar('\n');
    terminal_print("  Access: "); terminal_print(device.read_only ? "read-only\n" : "read/write\n");
}

static void command_disk_read(const char* text)
{
    char first[32];
    char second[32];
    usize first_len = 0u;
    usize second_len = 0u;

    while (*text == ' ') text++;
    while (*text != '\0' && *text != ' ' && first_len + 1u < sizeof(first)) first[first_len++] = *text++;
    first[first_len] = '\0';
    while (*text == ' ') text++;
    while (*text != '\0' && *text != ' ' && second_len + 1u < sizeof(second)) second[second_len++] = *text++;
    second[second_len] = '\0';

    u32 id = 0u;
    u32 lba = 0u;
    if (!shell_parse_u32_decimal(first, &id) || !shell_parse_u32_decimal(second, &lba))
    {
        terminal_print("Usage: disk read <id> <lba>\n");
        return;
    }

    RootBlockDevice device;
    if (!block_device_get_by_id(id, &device))
    {
        terminal_print("disk: device not found\n");
        return;
    }

    if (device.block_size > 4096u)
    {
        terminal_print("disk: block too large for debug reader\n");
        return;
    }

    static u8 buffer[4096];
    root_memzero(buffer, sizeof(buffer));
    if (!block_device_read(id, (u64)lba, 1u, buffer))
    {
        terminal_print("disk: read failed\n");
        return;
    }

    usize preview = device.block_size > 256u ? 256u : device.block_size;
    terminal_print("First "); shell_print_u32((u32)preview);
    terminal_print(" bytes of disk "); shell_print_u32(id);
    terminal_print(" LBA "); shell_print_u32(lba); terminal_print(":\n");

    for (usize offset = 0u; offset < preview; offset += 16u)
    {
        shell_print_hex_u32_width((u32)offset, 4u);
        terminal_print(": ");
        usize line_end = offset + 16u;
        if (line_end > preview) line_end = preview;
        for (usize i = offset; i < line_end; i++)
        {
            shell_print_hex_u32_width(buffer[i], 2u);
            terminal_putchar(' ');
        }
        terminal_putchar('\n');
    }
}

static void command_disk(const char* arguments)
{
    while (*arguments == ' ') arguments++;
    if (root_starts_with(arguments, "info "))
    {
        command_disk_info(arguments + 5);
        return;
    }
    if (root_starts_with(arguments, "read "))
    {
        command_disk_read(arguments + 5);
        return;
    }
    terminal_print("Usage: disk info <id> | disk read <id> <lba>\n");
}

static void command_run(
    const char* arguments
)
{
    const char* cursor = arguments;
    int argc = 0;

    while (
        argc < (int)SHELL_RUN_MAX_ARGS
        &&
        shell_read_argument(
            &cursor,
            run_arg_storage[argc],
            sizeof(run_arg_storage[argc])
        )
    )
    {
        run_argv[argc] = run_arg_storage[argc];
        argc++;

        if (*cursor == '\0')
        {
            break;
        }
    }

    if (argc == 0)
    {
        terminal_print("Usage: run <app|file.elf> [args]\n");
        return;
    }

    if (*cursor != '\0')
    {
        terminal_print("run: too many or invalid arguments\n");
        return;
    }

    RootAppRunInfo result = app_manager_run(
        run_argv[0],
        argc,
        run_argv
    );

    if (result.result != ROOT_APP_RUN_OK)
    {
        terminal_print("run: ");

        if (result.result == ROOT_APP_RUN_ELF_ERROR)
        {
            terminal_print(
                elf_loader_result_string(result.elf_result)
            );
        }
        else if (result.result == ROOT_APP_RUN_BAD_MANIFEST)
        {
            terminal_print("invalid app.toml");
        }
        else if (result.result == ROOT_APP_RUN_INVALID_NAME)
        {
            terminal_print("invalid application name/path");
        }
        else
        {
            terminal_print("application not found");
        }

        terminal_putchar('\n');
        return;
    }

    terminal_print("[process ");
    shell_print_u32(result.pid);
    terminal_print(" exited ");
    shell_print_i32(result.exit_code);
    terminal_print("]\n");
}


static void command_package(
    const char* arguments
)
{
    const char* cursor = arguments;
    char action[32];

    if (
        !shell_read_argument(
            &cursor,
            action,
            sizeof(action)
        )
    )
    {
        terminal_print(
            "Usage:\n"
            "  package install <file.rtpgk>\n"
            "  package remove <name>\n"
            "  package list\n"
            "  package info <name>\n"
        );
        return;
    }

    if (root_streq(action, "list"))
    {
        usize count = package_count();

        if (count == 0u)
        {
            terminal_print("No packages installed.\n");
            return;
        }

        for (usize i = 0; i < count; i++)
        {
            RootPackageInfo info;

            if (!package_get(i, &info))
            {
                continue;
            }

            terminal_print(info.name);
            terminal_putchar(' ');
            terminal_print(info.version);
            terminal_putchar('\n');
        }

        return;
    }

    char value[ROOT_PATH_MAX];

    if (
        !shell_read_argument(
            &cursor,
            value,
            sizeof(value)
        )
    )
    {
        terminal_print("package: missing name/path\n");
        return;
    }

    if (root_streq(action, "install"))
    {
        RootPackageResult result = package_install(value);

        if (result == ROOT_PACKAGE_OK)
        {
            terminal_print("Package installed.\n");
        }
        else
        {
            terminal_print("package: ");
            terminal_print(package_result_string(result));
            terminal_putchar('\n');
        }

        return;
    }

    if (root_streq(action, "remove"))
    {
        RootPackageResult result = package_remove(value);

        if (result == ROOT_PACKAGE_OK)
        {
            terminal_print("Package removed.\n");
        }
        else
        {
            terminal_print("package: ");
            terminal_print(package_result_string(result));
            terminal_putchar('\n');
        }

        return;
    }

    if (root_streq(action, "info"))
    {
        RootPackageInfo info;

        if (!package_find(value, &info))
        {
            terminal_print("package: package is not installed\n");
            return;
        }

        terminal_print("Name: ");
        terminal_print(info.name);
        terminal_print("\nVersion: ");
        terminal_print(info.version);
        terminal_print("\nEntry: ");
        terminal_print(info.entry);
        terminal_putchar('\n');
        return;
    }

    terminal_print("package: unknown action\n");
}


static void command_reboot(void)
{
    terminal_print(
        "Reiniciando...\n"
    );


    while (
        inb(0x64) & 0x02
    )
    {
    }


    outb(
        0x64,
        0xFE
    );


    while (1)
    {
        __asm__ volatile("hlt");
    }
}


static void command_shutdown(void)
{
    terminal_print(
        "Apagando...\n"
    );


    outw(
        0x604,
        0x2000
    );


    outw(
        0xB004,
        0x2000
    );


    while (1)
    {
        __asm__ volatile("hlt");
    }
}


static const char* shell_skip_spaces(
    const char* text
)
{
    while (
        *text == ' '
        ||
        *text == '\t'
    )
    {
        text++;
    }


    return text;
}



static bool shell_read_argument(
    const char** cursor,
    char* output,
    usize output_size
)
{
    const char* text =
        shell_skip_spaces(
            *cursor
        );


    if (
        *text == '\0'
    )
    {
        return false;
    }


    bool quoted =
        false;


    if (
        *text == '"'
    )
    {
        quoted = true;

        text++;
    }


    usize length = 0;


    while (*text)
    {
        if (
            quoted
        )
        {
            if (
                *text == '"'
            )
            {
                text++;

                break;
            }
        }

        else
        {
            if (
                *text == ' '
                ||
                *text == '\t'
            )
            {
                break;
            }
        }


        if (
            length
            >=
            output_size - 1
        )
        {
            return false;
        }


        output[
            length++
        ] =
            *text;


        text++;
    }


    output[
        length
    ] =
        '\0';


    *cursor =
        shell_skip_spaces(
            text
        );


    return
        length > 0;
}

static void shell_print_fs_result(
    FsResult result
)
{
    switch (
        result
    )
    {
        case FS_RESULT_OK:

            terminal_print(
                "Done.\n"
            );

            break;


        case FS_RESULT_NOT_FOUND:

            terminal_print(
                "Not found.\n"
            );

            break;


        case FS_RESULT_ALREADY_EXISTS:

            terminal_print(
                "Already exists.\n"
            );

            break;


        case FS_RESULT_NOT_DIRECTORY:

            terminal_print(
                "A path component is not a folder.\n"
            );

            break;


        case FS_RESULT_DIRECTORY_NOT_EMPTY:

            terminal_print(
                "Folder is not empty. Use --recursive.\n"
            );

            break;


        case FS_RESULT_INVALID_PATH:

            terminal_print(
                "Invalid path.\n"
            );

            break;


        case FS_RESULT_NO_SPACE:

            terminal_print(
                "No space available.\n"
            );

            break;


        case FS_RESULT_BUSY:

            terminal_print(
                "Resource is busy or protected.\n"
            );

            break;


        case FS_RESULT_NOT_FILE:

            terminal_print(
                "Path is not a file.\n"
            );

            break;


        case FS_RESULT_FILE_TOO_LARGE:

            terminal_print(
                "File is too large.\n"
            );

            break;


        case FS_RESULT_IO_ERROR:

            terminal_print(
                "Storage I/O error.\n"
            );

            break;


        default:

            terminal_print(
                "Filesystem error.\n"
            );

            break;
    }
}

static void command_create(
    const char* arguments
)
{
    const char* cursor =
        arguments;


    char mode[32];

    char path[
        ROOT_PATH_MAX
    ];


    if (
        !shell_read_argument(
            &cursor,
            mode,
            sizeof(mode)
        )
        ||
        !shell_read_argument(
            &cursor,
            path,
            sizeof(path)
        )
    )
    {
        terminal_print(
            "Usage:\n"
            "  create --file <path>\n"
            "  create --folder <path>\n"
        );

        return;
    }


    if (
        root_streq(
            mode,
            "--file"
        )
    )
    {
        shell_print_fs_result(
            filesystem_create_file(
                path
            )
        );

        return;
    }


    if (
        root_streq(
            mode,
            "--folder"
        )
    )
    {
        shell_print_fs_result(
            filesystem_create_directory(
                path
            )
        );

        return;
    }


    terminal_print(
        "Unknown create mode.\n"
    );
}

static void command_remove(
    const char* arguments
)
{
    const char* cursor =
        arguments;


    char first[
        ROOT_PATH_MAX
    ];


    if (
        !shell_read_argument(
            &cursor,
            first,
            sizeof(first)
        )
    )
    {
        terminal_print(
            "Usage:\n"
            "  remove <path>\n"
            "  remove --recursive <path>\n"
        );

        return;
    }


    bool recursive =
        false;


    char path[
        ROOT_PATH_MAX
    ];


    if (
        root_streq(
            first,
            "--recursive"
        )
        ||
        root_streq(
            first,
            "-r"
        )
    )
    {
        recursive =
            true;


        if (
            !shell_read_argument(
                &cursor,
                path,
                sizeof(path)
            )
        )
        {
            terminal_print(
                "Missing path.\n"
            );

            return;
        }
    }

    else
    {
        root_strlcpy(
            path,
            first,
            ROOT_PATH_MAX
        );
    }


    shell_print_fs_result(
        filesystem_remove(
            path,
            recursive
        )
    );
}

static void command_copy(
    const char* arguments
)
{
    const char* cursor =
        arguments;


    char source[
        ROOT_PATH_MAX
    ];


    char destination[
        ROOT_PATH_MAX
    ];


    if (
        !shell_read_argument(
            &cursor,
            source,
            sizeof(source)
        )
        ||
        !shell_read_argument(
            &cursor,
            destination,
            sizeof(destination)
        )
    )
    {
        terminal_print(
            "Usage: copy <source> <destination>\n"
        );

        return;
    }


    shell_print_fs_result(
        filesystem_copy(
            source,
            destination
        )
    );
}

static void command_move(
    const char* arguments
)
{
    const char* cursor =
        arguments;


    char source[
        ROOT_PATH_MAX
    ];


    char destination[
        ROOT_PATH_MAX
    ];


    if (
        !shell_read_argument(
            &cursor,
            source,
            sizeof(source)
        )
        ||
        !shell_read_argument(
            &cursor,
            destination,
            sizeof(destination)
        )
    )
    {
        terminal_print(
            "Usage: move <source> <destination>\n"
        );

        return;
    }


    shell_print_fs_result(
        filesystem_move(
            source,
            destination
        )
    );
}

/*
 * ============================================================
 * READFILE
 * ============================================================
 */

static void command_readfile(
    const char* arguments
)
{
    const char* cursor =
        arguments;


    char path[
        ROOT_PATH_MAX
    ];


    if (
        !shell_read_argument(
            &cursor,
            path,
            sizeof(path)
        )
    )
    {
        terminal_print(
            "Usage: readfile <path>\n"
        );

        return;
    }


    static char file_buffer[
        FS_MAX_FILE_SIZE + 1
    ];


    usize size =
        0;


    FsResult result =
        filesystem_read_file(
            path,
            file_buffer,
            sizeof(file_buffer),
            &size
        );


    if (
        result
        !=
        FS_RESULT_OK
    )
    {
        shell_print_fs_result(
            result
        );

        return;
    }


    terminal_print(
        file_buffer
    );


    if (
        size == 0
        ||
        file_buffer[
            size - 1
        ]
        !=
        '\n'
    )
    {
        terminal_putchar(
            '\n'
        );
    }
}


/*
 * ============================================================
 * WRITEFILE
 * ============================================================
 */

static void command_writefile(
    const char* arguments
)
{
    const char* cursor =
        arguments;


    char path[
        ROOT_PATH_MAX
    ];


    char text[
        FS_MAX_FILE_SIZE + 1
    ];


    if (
        !shell_read_argument(
            &cursor,
            path,
            sizeof(path)
        )
        ||
        !shell_read_argument(
            &cursor,
            text,
            sizeof(text)
        )
    )
    {
        terminal_print(
            "Usage: writefile <path> \"text\"\n"
        );

        return;
    }


    shell_print_fs_result(
        filesystem_write_file(
            path,
            text,
            root_strlen(
                text
            )
        )
    );
}


/*
 * ============================================================
 * APPENDFILE
 * ============================================================
 */

static void command_appendfile(
    const char* arguments
)
{
    const char* cursor =
        arguments;


    char path[
        ROOT_PATH_MAX
    ];


    char text[
        FS_MAX_FILE_SIZE + 1
    ];


    if (
        !shell_read_argument(
            &cursor,
            path,
            sizeof(path)
        )
        ||
        !shell_read_argument(
            &cursor,
            text,
            sizeof(text)
        )
    )
    {
        terminal_print(
            "Usage: appendfile <path> \"text\"\n"
        );

        return;
    }


    shell_print_fs_result(
        filesystem_append_file(
            path,
            text,
            root_strlen(
                text
            )
        )
    );
}


/*
 * ============================================================
 * EDITFILE
 * ============================================================
 */

static void command_editfile(
    const char* arguments
)
{
    const char* cursor =
        arguments;


    char path[
        ROOT_PATH_MAX
    ];


    if (
        !shell_read_argument(
            &cursor,
            path,
            sizeof(path)
        )
    )
    {
        terminal_print(
            "Usage: editfile <path>\n"
        );

        return;
    }


    FsResult result =
        rootedit_open(
            path
        );


    if (
        result
        !=
        FS_RESULT_OK
    )
    {
        shell_print_fs_result(
            result
        );
    }
}

/*
 * =====================================
 * INTERPRETAR COMANDO
 * =====================================
 */


static void command_driverpack(void)
{
    if (!driver_store_available())
    {
        terminal_print("Driver Pack unavailable: ");
        terminal_print(driver_store_last_error());
        terminal_putchar('\n');
        return;
    }

    usize count = driver_store_entry_count();
    terminal_print("Driver Pack entries: ");
    shell_print_u32((u32)count);
    terminal_print("  loaded: ");
    shell_print_u32((u32)driver_store_loaded_count());
    terminal_putchar('\n');

    for (usize i = 0u; i < count; i++)
    {
        RootDriverPackEntryInfo info;
        if (!driver_store_get(i, &info)) continue;
        terminal_print("  ");
        shell_print_u32((u32)i);
        terminal_print(": ");
        terminal_print(info.name);
        terminal_print(info.loaded ? " [loaded] " : " [stored] ");
        terminal_print(info.description);
        terminal_putchar('\n');

        for (u8 m = 0u; m < info.match_count; m++)
        {
            terminal_print("      PCI ");
            shell_print_hex_u32_width(info.matches[m].vendor_id, 4u);
            terminal_putchar(':');
            shell_print_hex_u32_width(info.matches[m].device_id, 4u);
            terminal_print(" class=");
            shell_print_hex_u32_width(info.matches[m].class_code, 2u);
            terminal_putchar(':');
            shell_print_hex_u32_width(info.matches[m].subclass, 2u);
            terminal_putchar('\n');
        }
    }
}

static void execute_command(
    const char* command
)
{
    if (
        command == NULL
        ||
        command[0] == '\0'
    )
    {
        return;
    }


    if (
        root_streq(
            command,
            "help"
        )
    )
    {
        command_help();

        return;
    }


    if (
        root_streq(
            command,
            "clear"
        )
    )
    {
        terminal_clear();

        return;
    }


    if (
        root_streq(
            command,
            "about"
        )
    )
    {
        command_about();

        return;
    }


    /*
     * ============================================================
     * ECHO
     * ============================================================
     */

    if (
        root_streq(
            command,
            "echo"
        )
    )
    {
        terminal_putchar(
            '\n'
        );

        return;
    }


    if (
        root_starts_with(
            command,
            "echo "
        )
    )
    {
        command_echo(
            command + 5
        );

        return;
    }


    /*
     * ============================================================
     * GODIR
     * ============================================================
     */

    if (
        root_streq(
            command,
            "godir"
        )
    )
    {
        terminal_print(
            "Usage: godir <path>\n"
        );

        return;
    }


    if (
        root_starts_with(
            command,
            "godir "
        )
    )
    {
        command_godir(
            command + 6
        );

        return;
    }


    /*
     * ============================================================
     * SEEDIR SEARCH
     * ============================================================
     */

    if (
        root_starts_with(
            command,
            "seedir(\""
        )
    )
    {
        command_find_directory(
            command
        );

        return;
    }


    if (
        root_streq(
            command,
            "seedir"
        )
    )
    {
        command_seedir();

        return;
    }


    /*
     * ============================================================
     * SEE
     * ============================================================
     */

    if (
        root_streq(
            command,
            "see"
        )
    )
    {
        command_see(
            ""
        );

        return;
    }


    if (
        root_starts_with(
            command,
            "see "
        )
    )
    {
        command_see(
            command + 4
        );

        return;
    }


    /*
     * ============================================================
     * CREATE
     * ============================================================
     */

    if (
        root_streq(
            command,
            "create"
        )
    )
    {
        terminal_print(
            "Usage:\n"
            "  create --file <path>\n"
            "  create --folder <path>\n"
        );

        return;
    }


    if (
        root_starts_with(
            command,
            "create "
        )
    )
    {
        command_create(
            command + 7
        );

        return;
    }


    /*
     * ============================================================
     * REMOVE
     * ============================================================
     */

    if (
        root_streq(
            command,
            "remove"
        )
    )
    {
        terminal_print(
            "Usage:\n"
            "  remove <path>\n"
            "  remove --recursive <path>\n"
            "  remove -r <path>\n"
        );

        return;
    }


    if (
        root_starts_with(
            command,
            "remove "
        )
    )
    {
        command_remove(
            command + 7
        );

        return;
    }


    /*
     * ============================================================
     * COPY
     * ============================================================
     */

    if (
        root_streq(
            command,
            "copy"
        )
    )
    {
        terminal_print(
            "Usage: copy <source> <destination>\n"
        );

        return;
    }


    if (
        root_starts_with(
            command,
            "copy "
        )
    )
    {
        command_copy(
            command + 5
        );

        return;
    }


    /*
     * ============================================================
     * MOVE
     * ============================================================
     */

    if (
        root_streq(
            command,
            "move"
        )
    )
    {
        terminal_print(
            "Usage: move <source> <destination>\n"
        );

        return;
    }


    if (
        root_starts_with(
            command,
            "move "
        )
    )
    {
        command_move(
            command + 5
        );

        return;
    }


    /*
     * ============================================================
     * READFILE
     * ============================================================
     */

    if (
        root_streq(
            command,
            "readfile"
        )
    )
    {
        terminal_print(
            "Usage: readfile <path>\n"
        );

        return;
    }


    if (
        root_starts_with(
            command,
            "readfile "
        )
    )
    {
        command_readfile(
            command + 9
        );

        return;
    }


    /*
     * ============================================================
     * WRITEFILE
     * ============================================================
     */

    if (
        root_streq(
            command,
            "writefile"
        )
    )
    {
        terminal_print(
            "Usage: writefile <path> \"text\"\n"
        );

        return;
    }


    if (
        root_starts_with(
            command,
            "writefile "
        )
    )
    {
        command_writefile(
            command + 10
        );

        return;
    }


    /*
     * ============================================================
     * APPENDFILE
     * ============================================================
     */

    if (
        root_streq(
            command,
            "appendfile"
        )
    )
    {
        terminal_print(
            "Usage: appendfile <path> \"text\"\n"
        );

        return;
    }


    if (
        root_starts_with(
            command,
            "appendfile "
        )
    )
    {
        command_appendfile(
            command + 11
        );

        return;
    }


    /*
     * ============================================================
     * EDITFILE
     * ============================================================
     */

    if (
        root_streq(
            command,
            "editfile"
        )
    )
    {
        terminal_print(
            "Usage: editfile <path>\n"
        );

        return;
    }


    if (
        root_starts_with(
            command,
            "editfile "
        )
    )
    {
        command_editfile(
            command + 9
        );

        return;
    }


    /*
     * ============================================================
     * PCI / DEVICE MANAGER
     * ============================================================
     */

    if (root_streq(command, "pci"))
    {
        command_pci("");
        return;
    }

    if (root_starts_with(command, "pci "))
    {
        command_pci(command + 4);
        return;
    }

    if (root_streq(command, "devices"))
    {
        command_devices();
        return;
    }

    if (root_streq(command, "device"))
    {
        terminal_print("Usage: device info <id>\n");
        return;
    }

    if (root_starts_with(command, "device "))
    {
        command_device(command + 7);
        return;
    }

    if (root_streq(command, "drivers"))
    {
        command_drivers();
        return;
    }

    if (root_streq(command, "driverpack"))
    {
        command_driverpack();
        return;
    }

    if (root_streq(command, "usb"))
    {
        command_usb("");
        return;
    }

    if (root_starts_with(command, "usb "))
    {
        command_usb(command + 4);
        return;
    }

    if (root_streq(command, "rndis"))
    {
        command_rndis();
        return;
    }


    if (root_streq(command, "disks"))
    {
        command_disks();
        return;
    }

    if (root_streq(command, "disk"))
    {
        command_disk("");
        return;
    }

    if (root_starts_with(command, "disk "))
    {
        command_disk(command + 5);
        return;
    }


    /*
     * ============================================================
     * NETWORK
     * ============================================================
     */

    if (root_streq(command, "net"))
    {
        command_net();
        return;
    }

    if (root_streq(command, "net devices"))
    {
        command_net_devices();
        return;
    }

    if (root_starts_with(command, "net use "))
    {
        command_net_use(command + 8);
        return;
    }

    if (root_streq(command, "ifconfig"))
    {
        command_ifconfig();
        return;
    }

    if (root_streq(command, "dhcp"))
    {
        command_dhcp();
        return;
    }

    if (root_streq(command, "arp"))
    {
        command_arp("");
        return;
    }

    if (root_starts_with(command, "arp "))
    {
        command_arp(command + 4);
        return;
    }

    if (root_streq(command, "dns"))
    {
        command_dns("");
        return;
    }

    if (root_starts_with(command, "dns "))
    {
        command_dns(command + 4);
        return;
    }

    if (root_streq(command, "tcp"))
    {
        command_tcp("");
        return;
    }

    if (root_starts_with(command, "tcp "))
    {
        command_tcp(command + 4);
        return;
    }


    /*
     * ============================================================
     * STORAGE
     * ============================================================
     */

    if (
        root_streq(
            command,
            "storage"
        )
    )
    {
        command_storage();

        return;
    }


    /*
     * ============================================================
     * RUN / PROCESSES
     * ============================================================
     */

    if (root_streq(command, "run"))
    {
        terminal_print("Usage: run <app|file.elf> [args]\n");
        return;
    }

    if (root_starts_with(command, "run "))
    {
        command_run(command + 4);
        return;
    }

    if (root_streq(command, "ps"))
    {
        command_ps();
        return;
    }


    /*
     * ============================================================
     * PACKAGE MANAGER
     * ============================================================
     */

    if (root_streq(command, "package"))
    {
        command_package("");
        return;
    }

    if (root_starts_with(command, "package "))
    {
        command_package(command + 8);
        return;
    }


    /*
     * ============================================================
     * REBOOT
     * ============================================================
     */

    if (
        root_streq(
            command,
            "reboot"
        )
    )
    {
        command_reboot();

        return;
    }


    /*
     * ============================================================
     * SHUTDOWN
     * ============================================================
     */

    if (
        root_streq(
            command,
            "shutdown"
        )
    )
    {
        command_shutdown();

        return;
    }


    /*
     * ============================================================
     * UNKNOWN
     * ============================================================
     */

    terminal_print(
        "Unknown command: "
    );


    terminal_print(
        command
    );


    terminal_putchar(
        '\n'
    );
}


static void shell_prompt(void)
{
    terminal_print(
        ROOTOS_DEFAULT_USER
    );

    terminal_putchar('@');

    terminal_print(
        ROOTOS_HOSTNAME
    );

    terminal_putchar(':');


    terminal_print(
        filesystem_current_directory()
    );


    terminal_print("$ ");


    if (
        terminal_get_col()
        >
        60
    )
    {
        terminal_putchar('\n');

        terminal_print("$ ");
    }


    input_start_row =
        terminal_get_row();

    input_start_col =
        terminal_get_col();

    rendered_length = 0;
}

/*
 * ============================================================
 * ANCHO VISUAL
 * ============================================================
 */

static u32 shell_cells_until(
    u32 index
)
{
    u32 cells = 0;


    if (
        index > command_length
    )
    {
        index =
            command_length;
    }


    for (
        u32 i = 0;
        i < index;
        i++
    )
    {
        cells +=
            terminal_codepoint_cells(
                command_buffer[i]
            );
    }


    return cells;
}


static u32 shell_total_cells(void)
{
    return
        shell_cells_until(
            command_length
        );
}


/*
 * ============================================================
 * CURSOR
 * ============================================================
 */

static void shell_update_cursor(void)
{
    terminal_set_cursor(
        input_start_col
        +
        shell_cells_until(
            command_cursor
        ),

        input_start_row
    );
}


/*
 * ============================================================
 * REDRAW
 * ============================================================
 */

static void shell_redraw_line(void)
{
    terminal_set_cursor(
        input_start_col,
        input_start_row
    );


    for (
        u32 i = 0;
        i < command_length;
        i++
    )
    {
        terminal_putcodepoint(
            command_buffer[i]
        );
    }


    u32 current_cells =
        shell_total_cells();


    for (
        u32 i = current_cells;
        i < rendered_length;
        i++
    )
    {
        terminal_putchar(
            ' '
        );
    }


    rendered_length =
        current_cells;


    shell_update_cursor();
}

/*
 * ============================================================
 * HISTORY - LOAD BUFFER
 * ============================================================
 */

static void shell_history_load(
    u32 history_index
)
{
    if (
        history_index
        >=
        command_history_count
    )
    {
        return;
    }


    command_length =
        command_history_length[
            history_index
        ];


    for (
        u32 i = 0;
        i < command_length;
        i++
    )
    {
        command_buffer[i] =
            command_history[
                history_index
            ][
                i
            ];
    }


    command_buffer[
        command_length
    ] =
        0;


    command_cursor =
        command_length;


    shell_redraw_line();
}


/*
 * ============================================================
 * HISTORY - ADD CURRENT COMMAND
 * ============================================================
 */

static void shell_history_add_current(void)
{
    if (
        command_length == 0
    )
    {
        command_history_position =
            -1;


        return;
    }


    /*
     * Evitar guardar dos comandos
     * consecutivos idénticos.
     */

    if (
        command_history_count > 0
    )
    {
        u32 last =
            command_history_count - 1;


        if (
            command_history_length[last]
            ==
            command_length
        )
        {
            bool equal =
                true;


            for (
                u32 i = 0;
                i < command_length;
                i++
            )
            {
                if (
                    command_history[last][i]
                    !=
                    command_buffer[i]
                )
                {
                    equal =
                        false;


                    break;
                }
            }


            if (
                equal
            )
            {
                command_history_position =
                    -1;


                return;
            }
        }
    }


    /*
     * Cola llena:
     * eliminar comando más antiguo.
     */

    if (
        command_history_count
        >=
        SHELL_HISTORY_SIZE
    )
    {
        for (
            u32 i = 1;
            i < SHELL_HISTORY_SIZE;
            i++
        )
        {
            command_history_length[
                i - 1
            ] =
                command_history_length[
                    i
                ];


            for (
                u32 j = 0;
                j <
                command_history_length[i];

                j++
            )
            {
                command_history[
                    i - 1
                ][
                    j
                ] =
                    command_history[
                        i
                    ][
                        j
                    ];
            }
        }


        command_history_count =
            SHELL_HISTORY_SIZE - 1;
    }


    u32 index =
        command_history_count;


    command_history_length[
        index
    ] =
        command_length;


    for (
        u32 i = 0;
        i < command_length;
        i++
    )
    {
        command_history[
            index
        ][
            i
        ] =
            command_buffer[i];
    }


    command_history_count++;


    command_history_position =
        -1;
}


/*
 * ============================================================
 * HISTORY - PREVIOUS
 * ============================================================
 */

static void shell_history_previous(void)
{
    if (
        command_history_count == 0
    )
    {
        return;
    }


    if (
        command_history_position < 0
    )
    {
        command_history_position =
            (i32)command_history_count
            -
            1;
    }

    else if (
        command_history_position > 0
    )
    {
        command_history_position--;
    }


    shell_history_load(
        (u32)command_history_position
    );
}


/*
 * ============================================================
 * HISTORY - NEXT
 * ============================================================
 */

static void shell_history_next(void)
{
    if (
        command_history_position < 0
    )
    {
        return;
    }


    if (
        command_history_position
        <
        (i32)command_history_count - 1
    )
    {
        command_history_position++;


        shell_history_load(
            (u32)command_history_position
        );


        return;
    }


    command_history_position =
        -1;


    command_length =
        0;


    command_cursor =
        0;


    command_buffer[0] =
        0;


    shell_redraw_line();
}

/*
 * ============================================================
 * INSERTAR UNICODE
 * ============================================================
 */

static bool shell_insert_codepoint_raw(
    RootCodepoint codepoint
)
{
    if (!root_unicode_valid(codepoint))
        return false;

    if (command_length >= COMMAND_BUFFER_SIZE - 1)
        return false;

    u32 needed = terminal_codepoint_cells(codepoint);
    u32 total = shell_total_cells();
    u32 columns = terminal_get_columns();

    /* Shell input is intentionally one visual line for now. */
    if (
        input_start_col + total + needed >= columns
    )
    {
        return false;
    }

    for (u32 i = command_length; i > command_cursor; i--)
        command_buffer[i] = command_buffer[i - 1];

    command_buffer[command_cursor] = codepoint;
    command_cursor++;
    command_length++;
    command_buffer[command_length] = 0;
    return true;
}


static void shell_insert_codepoint(
    RootCodepoint codepoint
)
{
    if (shell_insert_codepoint_raw(codepoint))
        shell_redraw_line();
}


static void shell_paste_clipboard(void)
{
    const RootCodepoint* data = rootclipboard_data();
    usize length = rootclipboard_length();

    if (data == NULL || length == 0)
        return;

    bool changed = false;

    for (usize i = 0; i < length; i++)
    {
        RootCodepoint codepoint = data[i];

        /* Current shell editor is single-line. */
        if (codepoint == '\n' || codepoint == '\r')
            codepoint = ' ';

        if (!shell_insert_codepoint_raw(codepoint))
            break;

        changed = true;
    }

    if (changed)
    {
        command_history_position = -1;
        shell_redraw_line();
    }
}


/*
 * ============================================================
 * BACKSPACE
 * ============================================================
 */

static void shell_backspace(void)
{
    if (
        command_cursor == 0
    )
    {
        return;
    }


    for (
        u32 i =
            command_cursor - 1;

        i <
            command_length - 1;

        i++
    )
    {
        command_buffer[i] =
            command_buffer[
                i + 1
            ];
    }


    command_cursor--;

    command_length--;


    command_buffer[
        command_length
    ] =
        0;


    shell_redraw_line();
}


/*
 * ============================================================
 * DELETE
 * ============================================================
 */

static void shell_delete(void)
{
    if (
        command_cursor
        >=
        command_length
    )
    {
        return;
    }


    for (
        u32 i =
            command_cursor;

        i <
            command_length - 1;

        i++
    )
    {
        command_buffer[i] =
            command_buffer[
                i + 1
            ];
    }


    command_length--;


    command_buffer[
        command_length
    ] =
        0;


    shell_redraw_line();
}


/*
 * ============================================================
 * CLEAR INPUT
 * ============================================================
 */

static void shell_clear_input(void)
{
    command_length = 0;

    command_cursor = 0;

    command_buffer[0] = 0;


    shell_redraw_line();
}


/*
 * ============================================================
 * UNICODE -> UTF-8 PARA LOS COMANDOS
 * ============================================================
 */

static bool shell_build_utf8(void)
{
    usize output = 0;


    for (
        u32 i = 0;
        i < command_length;
        i++
    )
    {
        char encoded[4];


        usize count =
            root_utf8_encode(
                command_buffer[i],
                encoded
            );


        if (
            output
            +
            count
            >=
            COMMAND_UTF8_SIZE
        )
        {
            return false;
        }


        for (
            usize j = 0;
            j < count;
            j++
        )
        {
            command_utf8[
                output++
            ] =
                encoded[j];
        }
    }


    command_utf8[
        output
    ] =
        '\0';


    return true;
}


/*
 * ============================================================
 * MOUSE -> CURSOR DE TEXTO
 * ============================================================
 */

static void shell_mouse_click(
    const RootInputEvent* event
)
{
    if (
        event->button
        !=
        ROOT_MOUSE_LEFT
    )
    {
        return;
    }


    u32 column;
    u32 row;


    if (
        !terminal_pixel_to_cell(
            event->mouse_x,
            event->mouse_y,
            &column,
            &row
        )
    )
    {
        return;
    }


    if (
        row
        !=
        input_start_row
    )
    {
        return;
    }


    if (
        column
        <
        input_start_col
    )
    {
        return;
    }


    u32 target =
        column
        -
        input_start_col;


    u32 position = 0;

    u32 cells = 0;


    while (
        position
        <
        command_length
    )
    {
        u32 width =
            terminal_codepoint_cells(
                command_buffer[
                    position
                ]
            );


        if (
            target
            <
            cells
            +
            width
        )
        {
            break;
        }


        cells +=
            width;

        position++;
    }


    command_cursor =
        position;


    shell_update_cursor();
}


/*
 * ============================================================
 * COMMAND OUTPUT BATCHING
 * ============================================================
 *
 * Most commands return to the shell immediately and can render their
 * output as one transaction. Full-screen/terminal-owning commands must
 * remain live while they run.
 */
static bool shell_command_can_batch(
    const char* command
)
{
    if (command == NULL)
        return false;

    if (
        root_streq(command, "reboot") ||
        root_streq(command, "shutdown") ||
        root_streq(command, "editfile") ||
        root_starts_with(command, "editfile ") ||
        root_streq(command, "run") ||
        root_starts_with(command, "run ") ||
        root_streq(command, "dhcp") ||
        root_streq(command, "dns") ||
        root_starts_with(command, "dns ") ||
        root_streq(command, "tcp") ||
        root_starts_with(command, "tcp connect ")
    )
    {
        return false;
    }

    return true;
}

/*
 * ============================================================
 * SHELL LOOP
 * ============================================================
 */

void shell_run(void)
{
    command_length = 0;
    command_cursor = 0;
    rendered_length = 0;
    command_buffer[0] = 0;

    command_history_count = 0;
    command_history_position = -1;

    shell_prompt();

    while (1)
    {
        /* USB must keep progressing even when the user is not typing.
         * PIT interrupts wake HLT periodically, so polling here gives us
         * hotplug/reconnect without a scheduler or USB IRQ worker yet. */
        xhci_poll();
        usb_service();
        rndis_service();

        RootInputEvent event;
        if (!rootinput_next_event(&event))
        {
            __asm__ volatile("hlt");
            continue;
        }

        /* ========================================================
         * TERMINAL MOUSE SELECTION
         * ======================================================== */

        if (
            event.type == ROOT_INPUT_MOUSE_BUTTON_DOWN &&
            event.button == ROOT_MOUSE_LEFT
        )
        {
            terminal_selection_begin(
                event.mouse_x,
                event.mouse_y
            );

            continue;
        }

        if (event.type == ROOT_INPUT_MOUSE_DRAG)
        {
            terminal_selection_drag(
                event.mouse_x,
                event.mouse_y
            );

            continue;
        }

        if (
            event.type == ROOT_INPUT_MOUSE_BUTTON_UP &&
            event.button == ROOT_MOUSE_LEFT
        )
        {
            terminal_selection_end();
            continue;
        }

        if (
            event.type == ROOT_INPUT_MOUSE_DOUBLE_CLICK &&
            event.button == ROOT_MOUSE_LEFT
        )
        {
            terminal_selection_select_word(
                event.mouse_x,
                event.mouse_y
            );

            continue;
        }

        if (event.type == ROOT_INPUT_MOUSE_CLICK)
        {
            /* A plain click in the active command line moves its caret. */
            if (!terminal_selection_active())
                shell_mouse_click(&event);

            continue;
        }

        /* Mouse wheel scrolls terminal history without changing the command. */
        if (event.type == ROOT_INPUT_MOUSE_WHEEL)
        {
            i32 delta = event.mouse_wheel;
            u32 steps = (u32)(delta < 0 ? -delta : delta);

            if (steps == 0)
                steps = 1;

            /* Three terminal lines per wheel notch. */
            u32 lines = steps * 3u;

            if (delta > 0)
                terminal_scrollback_up(lines);
            else
                terminal_scrollback_down(lines);

            continue;
        }

        if (event.type != ROOT_INPUT_KEY_DOWN)
            continue;

        /* ========================================================
         * STANDARD TERMINAL SCROLLBACK
         * ======================================================== */

        if (
            !event.ctrl &&
            !event.alt &&
            event.key == ROOT_KEY_PAGE_UP
        )
        {
            u32 amount = terminal_get_rows() / 2u;
            if (amount == 0)
                amount = 1;

            terminal_scrollback_up(amount);
            continue;
        }

        if (
            !event.ctrl &&
            !event.alt &&
            event.key == ROOT_KEY_PAGE_DOWN
        )
        {
            u32 amount = terminal_get_rows() / 2u;
            if (amount == 0)
                amount = 1;

            terminal_scrollback_down(amount);
            continue;
        }

        RootTextAction action = roottext_terminal_action(&event);

        /* Ctrl+Shift+C copies without returning from scrollback view. */
        if (action == ROOT_TEXT_ACTION_TERMINAL_COPY)
        {
            terminal_selection_copy();
            continue;
        }

        if (action == ROOT_TEXT_ACTION_TERMINAL_PASTE)
        {
            terminal_scrollback_bottom();
            terminal_selection_clear();
            shell_paste_clipboard();
            continue;
        }

        /* Keyboard editing always returns to the live prompt. */
        terminal_scrollback_bottom();

        if (action == ROOT_TEXT_ACTION_LINE_START)
        {
            command_cursor = 0;
            shell_update_cursor();
            continue;
        }

        if (action == ROOT_TEXT_ACTION_LINE_END)
        {
            command_cursor = command_length;
            shell_update_cursor();
            continue;
        }

        if (action == ROOT_TEXT_ACTION_LINE_CLEAR)
        {
            shell_clear_input();
            command_history_position = -1;
            continue;
        }

        if (action == ROOT_TEXT_ACTION_CLEAR_SCREEN)
        {
            terminal_clear();
            shell_prompt();
            shell_redraw_line();
            continue;
        }

        if (action == ROOT_TEXT_ACTION_INTERRUPT)
        {
            terminal_print("^C\n");

            command_length = 0;
            command_cursor = 0;
            rendered_length = 0;
            command_buffer[0] = 0;
            command_history_position = -1;

            shell_prompt();
            continue;
        }

        if (action == ROOT_TEXT_ACTION_SUSPEND)
        {
            /* Reserved for job control once processes exist. */
            continue;
        }

        /* Any other Ctrl/Alt combination is not text. */
        if (event.ctrl || (event.alt && !event.altgr))
            continue;

        /* ========================================================
         * HISTORY
         * ======================================================== */

        if (event.key == ROOT_KEY_UP)
        {
            shell_history_previous();
            continue;
        }

        if (event.key == ROOT_KEY_DOWN)
        {
            shell_history_next();
            continue;
        }

        /* ========================================================
         * ENTER
         * ======================================================== */

        if (
            event.key == ROOT_KEY_ENTER ||
            event.key == ROOT_KEY_KP_ENTER
        )
        {
            command_cursor = command_length;
            shell_update_cursor();
            terminal_putchar('\n');

            shell_history_add_current();

            if (shell_build_utf8())
            {
                bool batched =
                    shell_command_can_batch(command_utf8);

                if (batched)
                    terminal_begin_output_batch();

                execute_command(command_utf8);

                if (batched)
                    terminal_end_batch();
            }

            command_length = 0;
            command_cursor = 0;
            rendered_length = 0;
            command_buffer[0] = 0;
            command_history_position = -1;

            shell_prompt();
            continue;
        }

        /* ========================================================
         * CURSOR MOVEMENT
         * ======================================================== */

        if (event.key == ROOT_KEY_LEFT)
        {
            if (command_cursor > 0)
            {
                command_cursor--;
                shell_update_cursor();
            }

            continue;
        }

        if (event.key == ROOT_KEY_RIGHT)
        {
            if (command_cursor < command_length)
            {
                command_cursor++;
                shell_update_cursor();
            }

            continue;
        }

        if (event.key == ROOT_KEY_HOME)
        {
            command_cursor = 0;
            shell_update_cursor();
            continue;
        }

        if (event.key == ROOT_KEY_END)
        {
            command_cursor = command_length;
            shell_update_cursor();
            continue;
        }

        /* ========================================================
         * DELETE
         * ======================================================== */

        if (event.key == ROOT_KEY_BACKSPACE)
        {
            command_history_position = -1;
            shell_backspace();
            continue;
        }

        if (event.key == ROOT_KEY_DELETE)
        {
            command_history_position = -1;
            shell_delete();
            continue;
        }

        /* ========================================================
         * TEXT
         * ======================================================== */

        if (roottext_should_insert(&event))
        {
            command_history_position = -1;
            shell_insert_codepoint(event.codepoint);
            continue;
        }
    }
}
