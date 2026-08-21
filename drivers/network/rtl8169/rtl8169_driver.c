#include <rootos/rootdriver.h>

/*
 * RootOS RTL8168/RTL8111 driver - v0.47-2
 *
 * Polling driver for PCI 10ec:8168.  The primary physical target is the
 * RTL8111/8168 controller found in the ASUS test machine.  It deliberately
 * does not claim RTL8125/810x IDs.
 *
 * RTL8168H/8111H is detected through the hardware-version field in TxConfig;
 * for that family we apply the conservative ePHY table also used by the
 * upstream r8169 initialization path.  Firmware loading/OCP PHY scripts are
 * intentionally left for a later driver revision if real hardware requires
 * them.
 */

#define RTL_VENDOR_ID 0x10ECu
#define RTL_DEVICE_8168 0x8168u

#define RX_COUNT 32u
#define TX_COUNT 16u
#define BUFFER_SIZE 2048u

#define REG_MAC0              0x00u
#define REG_TX_DESC_LOW       0x20u
#define REG_TX_DESC_HIGH      0x24u
#define REG_CHIP_COMMAND      0x37u
#define REG_TX_POLL           0x38u
#define REG_INTR_MASK         0x3Cu
#define REG_INTR_STATUS       0x3Eu
#define REG_TX_CONFIG         0x40u
#define REG_RX_CONFIG         0x44u
#define REG_CFG9346           0x50u
#define REG_PHY_STATUS        0x6Cu
#define REG_EPHY_AR           0x80u
#define REG_RX_MAX_SIZE       0xDAu
#define REG_INTR_MITIGATE     0xE2u
#define REG_CPLUS_COMMAND     0xE0u
#define REG_RX_DESC_LOW       0xE4u
#define REG_RX_DESC_HIGH      0xE8u

#define CMD_RESET 0x10u
#define CMD_RX_ENABLE 0x08u
#define CMD_TX_ENABLE 0x04u

#define TX_POLL_NORMAL_QUEUE 0x40u

#define CFG9346_UNLOCK 0xC0u
#define CFG9346_LOCK   0x00u

#define PHY_LINK_UP 0x02u

#define RX_ACCEPT_BROADCAST 0x08u
#define RX_ACCEPT_MULTICAST 0x04u
#define RX_ACCEPT_MY_PHYS   0x02u
#define RX_FIFO_THRESHOLD   (7u << 13)
#define RX_DMA_BURST        (7u << 8)
#define TX_DMA_BURST        (7u << 8)
#define TX_INTERFRAME_GAP   (3u << 24)

#define DESC_OWN (1u << 31)
#define DESC_EOR (1u << 30)
#define DESC_FS  (1u << 29)
#define DESC_LS  (1u << 28)
#define RX_ERROR (1u << 21)
#define DESC_LENGTH_MASK 0x3FFFu

#define EPHYAR_FLAG 0x80000000u
#define EPHYAR_REG_SHIFT 16u
#define EPHYAR_REG_MASK 0x1Fu
#define EPHYAR_DATA_MASK 0xFFFFu

#define RTL_HWVER_MASK 0x7CFu
#define RTL_HWVER_8168H 0x541u
#define RTL_HWVER_8168M 0x6C0u

#define RTL_WAIT_LOOPS 200000u

typedef struct __attribute__((packed))
{
    RootDrvU32 opts1;
    RootDrvU32 opts2;
    RootDrvU64 address;
} RtlDescriptor;

typedef struct
{
    const RootDriverApi* api;
    RootDrvU64 mmio;
    RootDrvU8 mac[6];
    RootDrvU32 rx_index;
    RootDrvU32 tx_index;
    RootDrvU32 hardware_signature;
    RootDrvBool profile_8168h;
    RootDrvBool ready;
} RtlState;

static RtlState state;
static RtlDescriptor rx_descriptors[RX_COUNT] __attribute__((aligned(256)));
static RtlDescriptor tx_descriptors[TX_COUNT] __attribute__((aligned(256)));
static RootDrvU8 rx_buffers[RX_COUNT][BUFFER_SIZE] __attribute__((aligned(16)));
static RootDrvU8 tx_buffers[TX_COUNT][BUFFER_SIZE] __attribute__((aligned(16)));

static void memory_zero(void* pointer, RootDrvSize size)
{
    RootDrvU8* output = (RootDrvU8*)pointer;
    for (RootDrvSize i = 0u; i < size; i++)
        output[i] = 0u;
}

static void memory_copy(void* destination, const void* source, RootDrvSize size)
{
    RootDrvU8* output = (RootDrvU8*)destination;
    const RootDrvU8* input = (const RootDrvU8*)source;
    for (RootDrvSize i = 0u; i < size; i++)
        output[i] = input[i];
}

static RootDrvU8 read8(RootDrvU32 offset)
{
    return state.api->mmio_read8(state.mmio, offset);
}

static RootDrvU16 read16(RootDrvU32 offset)
{
    return state.api->mmio_read16(state.mmio, offset);
}

static RootDrvU32 read32(RootDrvU32 offset)
{
    return state.api->mmio_read32(state.mmio, offset);
}

static void write8(RootDrvU32 offset, RootDrvU8 value)
{
    state.api->mmio_write8(state.mmio, offset, value);
}

static void write16(RootDrvU32 offset, RootDrvU16 value)
{
    state.api->mmio_write16(state.mmio, offset, value);
}

static void write32(RootDrvU32 offset, RootDrvU32 value)
{
    state.api->mmio_write32(state.mmio, offset, value);
}

static RootDrvBool valid_mac(void)
{
    RootDrvBool any = 0;
    RootDrvBool all_ff = 1;

    for (RootDrvU32 i = 0u; i < 6u; i++)
    {
        if (state.mac[i] != 0u)
            any = 1;
        if (state.mac[i] != 0xFFu)
            all_ff = 0;
    }

    return any && !all_ff && (state.mac[0] & 0x01u) == 0u;
}

static RootDrvU64 find_mmio(const RootDriverDeviceInfo* device)
{
    for (RootDrvU32 i = 0u; i < ROOT_DRIVER_MAX_BARS; i++)
    {
        const RootDriverBar* bar = &device->bars[i];
        if (
            bar->present
            && (bar->type == ROOT_DRIVER_BAR_MEMORY32 || bar->type == ROOT_DRIVER_BAR_MEMORY64)
            && bar->base != 0u
            && bar->base <= 0xFFFFFFFFull
        )
        {
            return bar->base;
        }
    }

    return 0u;
}

static RootDrvBool wait_reset_complete(void)
{
    for (RootDrvU32 i = 0u; i < RTL_WAIT_LOOPS; i++)
    {
        if ((read8(REG_CHIP_COMMAND) & CMD_RESET) == 0u)
            return 1;
        state.api->cpu_pause();
    }
    return 0;
}

static void short_hardware_delay(void)
{
    /* EPHY accesses in mature drivers leave a small settling delay between
     * operations.  RootDriver ABI v1 has no microsecond timer yet, so use a
     * bounded PAUSE loop instead of introducing an unbounded busy wait. */
    for (RootDrvU32 i = 0u; i < 4096u; i++)
        state.api->cpu_pause();
}

static RootDrvBool ephy_read(RootDrvU8 reg, RootDrvU16* value)
{
    if (value == 0)
        return 0;

    write32(
        REG_EPHY_AR,
        ((RootDrvU32)(reg & EPHYAR_REG_MASK) << EPHYAR_REG_SHIFT)
    );

    for (RootDrvU32 i = 0u; i < RTL_WAIT_LOOPS; i++)
    {
        RootDrvU32 current = read32(REG_EPHY_AR);
        if ((current & EPHYAR_FLAG) != 0u)
        {
            *value = (RootDrvU16)(current & EPHYAR_DATA_MASK);
            return 1;
        }
        state.api->cpu_pause();
    }

    return 0;
}

static RootDrvBool ephy_write(RootDrvU8 reg, RootDrvU16 value)
{
    write32(
        REG_EPHY_AR,
        EPHYAR_FLAG
        | ((RootDrvU32)(reg & EPHYAR_REG_MASK) << EPHYAR_REG_SHIFT)
        | (RootDrvU32)value
    );

    for (RootDrvU32 i = 0u; i < RTL_WAIT_LOOPS; i++)
    {
        if ((read32(REG_EPHY_AR) & EPHYAR_FLAG) == 0u)
        {
            short_hardware_delay();
            return 1;
        }
        state.api->cpu_pause();
    }

    return 0;
}

static RootDrvBool ephy_modify(RootDrvU8 reg, RootDrvU16 mask, RootDrvU16 bits)
{
    RootDrvU16 current = 0u;
    if (!ephy_read(reg, &current))
        return 0;

    current = (RootDrvU16)((current & (RootDrvU16)(~mask)) | bits);
    return ephy_write(reg, current);
}

static RootDrvBool apply_8168h_ephy_profile(void)
{
    /* Safe early RTL8168H/8111H ePHY settings mirrored from the common
     * upstream r8169 hardware-start path. */
    static const struct
    {
        RootDrvU8 reg;
        RootDrvU16 mask;
        RootDrvU16 bits;
    } table[] =
    {
        { 0x1Eu, 0x0800u, 0x0001u },
        { 0x1Du, 0x0000u, 0x0800u },
        { 0x05u, 0xFFFFu, 0x2089u },
        { 0x06u, 0xFFFFu, 0x5881u },
        { 0x04u, 0xFFFFu, 0x854Au },
        { 0x01u, 0xFFFFu, 0x068Bu }
    };

    for (RootDrvU32 i = 0u; i < (RootDrvU32)(sizeof(table) / sizeof(table[0])); i++)
    {
        if (!ephy_modify(table[i].reg, table[i].mask, table[i].bits))
            return 0;
    }

    return 1;
}

static void initialize_descriptor_rings(void)
{
    memory_zero(rx_descriptors, sizeof(rx_descriptors));
    memory_zero(tx_descriptors, sizeof(tx_descriptors));

    for (RootDrvU32 i = 0u; i < RX_COUNT; i++)
    {
        rx_descriptors[i].address =
            (RootDrvU64)(RootDrvU32)(RootDrvSize)&rx_buffers[i][0];
        rx_descriptors[i].opts2 = 0u;
        rx_descriptors[i].opts1 =
            DESC_OWN
            | BUFFER_SIZE
            | (i == RX_COUNT - 1u ? DESC_EOR : 0u);
    }

    for (RootDrvU32 i = 0u; i < TX_COUNT; i++)
    {
        tx_descriptors[i].address =
            (RootDrvU64)(RootDrvU32)(RootDrvSize)&tx_buffers[i][0];
        tx_descriptors[i].opts2 = 0u;
        tx_descriptors[i].opts1 =
            i == TX_COUNT - 1u ? DESC_EOR : 0u;
    }

    state.api->memory_barrier();
}

static void program_descriptor_bases(void)
{
    RootDrvU64 rx_base =
        (RootDrvU64)(RootDrvU32)(RootDrvSize)&rx_descriptors[0];
    RootDrvU64 tx_base =
        (RootDrvU64)(RootDrvU32)(RootDrvSize)&tx_descriptors[0];

    /* r8169 programs the high dword before the low dword.  Preserve that
     * ordering even though RootOS is currently constrained below 4 GiB. */
    write32(REG_RX_DESC_HIGH, (RootDrvU32)(rx_base >> 32));
    write32(REG_RX_DESC_LOW, (RootDrvU32)rx_base);
    write32(REG_TX_DESC_HIGH, (RootDrvU32)(tx_base >> 32));
    write32(REG_TX_DESC_LOW, (RootDrvU32)tx_base);
}

static RootDrvBool net_ready(void* context)
{
    (void)context;
    return state.ready;
}

static RootDrvBool net_link_up(void* context)
{
    (void)context;
    return state.ready && (read8(REG_PHY_STATUS) & PHY_LINK_UP) != 0u;
}

static RootDrvBool net_send(
    void* context,
    const void* data,
    RootDrvSize size
)
{
    (void)context;

    if (!state.ready || data == 0 || size == 0u || size > BUFFER_SIZE)
        return 0;

    RtlDescriptor* descriptor = &tx_descriptors[state.tx_index];
    if ((descriptor->opts1 & DESC_OWN) != 0u)
        return 0;

    memory_copy(tx_buffers[state.tx_index], data, size);
    descriptor->opts2 = 0u;

    RootDrvU32 flags =
        DESC_OWN | DESC_FS | DESC_LS | (RootDrvU32)size;
    if (state.tx_index == TX_COUNT - 1u)
        flags |= DESC_EOR;

    state.api->memory_barrier();
    descriptor->opts1 = flags;
    state.api->memory_barrier();

    RootDrvU32 current = state.tx_index;
    state.tx_index = (state.tx_index + 1u) & (TX_COUNT - 1u);

    write8(REG_TX_POLL, TX_POLL_NORMAL_QUEUE);

    for (RootDrvU32 i = 0u; i < RTL_WAIT_LOOPS; i++)
    {
        if ((tx_descriptors[current].opts1 & DESC_OWN) == 0u)
            return 1;
        state.api->cpu_pause();
    }

    return 0;
}

static RootDrvBool net_receive(
    void* context,
    void* output,
    RootDrvSize capacity,
    RootDrvSize* result_size
)
{
    (void)context;

    if (result_size != 0)
        *result_size = 0u;

    if (!state.ready || output == 0)
        return 0;

    RtlDescriptor* descriptor = &rx_descriptors[state.rx_index];
    RootDrvU32 status = descriptor->opts1;

    if ((status & DESC_OWN) != 0u)
        return 0;

    RootDrvSize length = (RootDrvSize)(status & DESC_LENGTH_MASK);
    RootDrvBool good =
        (status & RX_ERROR) == 0u
        && (status & DESC_FS) != 0u
        && (status & DESC_LS) != 0u
        && length >= 4u;

    if (good)
        length -= 4u; /* hardware includes Ethernet FCS */

    if (good && length <= capacity)
    {
        memory_copy(output, rx_buffers[state.rx_index], length);
        if (result_size != 0)
            *result_size = length;
    }
    else
    {
        good = 0;
    }

    descriptor->opts2 = 0u;
    state.api->memory_barrier();
    descriptor->opts1 =
        DESC_OWN
        | BUFFER_SIZE
        | (state.rx_index == RX_COUNT - 1u ? DESC_EOR : 0u);
    state.api->memory_barrier();

    state.rx_index = (state.rx_index + 1u) & (RX_COUNT - 1u);
    return good;
}

static const RootNetDriverOps net_ops =
{
    .ready = net_ready,
    .link_up = net_link_up,
    .send_frame = net_send,
    .receive_frame = net_receive
};

int root_driver_entry(
    const RootDriverApi* api,
    const RootDriverDeviceInfo* device
)
{
    if (
        api == 0
        || api->abi_version != ROOT_DRIVER_ABI_VERSION
        || device == 0
        || device->bus_type != ROOT_DRIVER_BUS_PCI
        || device->vendor_id != RTL_VENDOR_ID
        || device->device_id != RTL_DEVICE_8168
    )
    {
        return ROOT_DRIVER_ERROR;
    }

    memory_zero(&state, sizeof(state));
    state.api = api;
    state.mmio = find_mmio(device);

    if (state.mmio == 0u)
    {
        api->log("rtl8169: no usable MMIO BAR\n");
        return ROOT_DRIVER_ERROR;
    }

    if (
        !api->pci_enable_memory(
            device->pci_bus,
            device->pci_device,
            device->pci_function
        )
        || !api->pci_enable_bus_master(
            device->pci_bus,
            device->pci_device,
            device->pci_function
        )
    )
    {
        api->log("rtl8169: could not enable PCI MMIO/bus master\n");
        return ROOT_DRIVER_ERROR;
    }

    write16(REG_INTR_MASK, 0u);
    write16(REG_INTR_STATUS, 0xFFFFu);
    write8(REG_CHIP_COMMAND, CMD_RESET);

    if (!wait_reset_complete())
    {
        api->log("rtl8169: reset timeout\n");
        return ROOT_DRIVER_ERROR;
    }

    state.hardware_signature =
        (read32(REG_TX_CONFIG) >> 20) & RTL_HWVER_MASK;
    state.profile_8168h =
        state.hardware_signature == RTL_HWVER_8168H
        || state.hardware_signature == RTL_HWVER_8168M;

    if (state.profile_8168h)
        api->log("rtl8169: RTL8168H/8111H hardware profile\n");
    else
        api->log("rtl8169: generic RTL8168 profile\n");

    for (RootDrvU32 i = 0u; i < 6u; i++)
        state.mac[i] = read8(REG_MAC0 + i);

    if (!valid_mac())
    {
        api->log("rtl8169: invalid hardware MAC\n");
        return ROOT_DRIVER_ERROR;
    }

    write8(REG_CFG9346, CFG9346_UNLOCK);

    if (state.profile_8168h && !apply_8168h_ephy_profile())
    {
        write8(REG_CFG9346, CFG9346_LOCK);
        api->log("rtl8169: RTL8168H ePHY initialization failed\n");
        return ROOT_DRIVER_ERROR;
    }

    /* Polling driver: disable interrupt mitigation and device interrupts. */
    write16(REG_INTR_MITIGATE, 0u);
    write16(REG_INTR_MASK, 0u);
    write16(REG_INTR_STATUS, 0xFFFFu);

    write16(REG_RX_MAX_SIZE, BUFFER_SIZE);

    /* Keep checksum offload disabled for now; the RootOS IPv4 stack computes
     * checksums itself. */
    write16(
        REG_CPLUS_COMMAND,
        (RootDrvU16)(read16(REG_CPLUS_COMMAND) & (RootDrvU16)(~0x0040u))
    );

    write32(
        REG_RX_CONFIG,
        RX_FIFO_THRESHOLD
        | RX_DMA_BURST
        | RX_ACCEPT_BROADCAST
        | RX_ACCEPT_MULTICAST
        | RX_ACCEPT_MY_PHYS
    );

    write32(REG_TX_CONFIG, TX_INTERFRAME_GAP | TX_DMA_BURST);

    initialize_descriptor_rings();
    program_descriptor_bases();
    state.api->memory_barrier();

    state.rx_index = 0u;
    state.tx_index = 0u;

    write8(REG_CHIP_COMMAND, CMD_RX_ENABLE | CMD_TX_ENABLE);
    write8(REG_CFG9346, CFG9346_LOCK);
    state.api->memory_barrier();

    state.ready = 1;

    if (!api->net_register("rtl8169", &state, &net_ops, state.mac))
    {
        state.ready = 0;
        api->log("rtl8169: network registration failed\n");
        return ROOT_DRIVER_ERROR;
    }

    api->log("rtl8169: network adapter registered\n");
    return ROOT_DRIVER_OK;
}
