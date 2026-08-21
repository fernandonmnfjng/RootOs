#include "e1000.h"

#include "device_manager.h"
#include "memory.h"

#define E1000_VENDOR_INTEL 0x8086u
#define E1000_DEVICE_QEMU  0x100Eu

#define E1000_REG_CTRL   0x0000u
#define E1000_REG_STATUS 0x0008u
#define E1000_REG_ICR    0x00C0u
#define E1000_REG_IMC    0x00D8u
#define E1000_REG_RCTL   0x0100u
#define E1000_REG_TCTL   0x0400u
#define E1000_REG_TIPG   0x0410u
#define E1000_REG_RDBAL  0x2800u
#define E1000_REG_RDBAH  0x2804u
#define E1000_REG_RDLEN  0x2808u
#define E1000_REG_RDH    0x2810u
#define E1000_REG_RDT    0x2818u
#define E1000_REG_TDBAL  0x3800u
#define E1000_REG_TDBAH  0x3804u
#define E1000_REG_TDLEN  0x3808u
#define E1000_REG_TDH    0x3810u
#define E1000_REG_TDT    0x3818u
#define E1000_REG_RAL0   0x5400u
#define E1000_REG_RAH0   0x5404u

#define E1000_CTRL_SLU   (1u << 6)
#define E1000_RCTL_EN    (1u << 1)
#define E1000_RCTL_BAM   (1u << 15)
#define E1000_RCTL_SECRC (1u << 26)
#define E1000_TCTL_EN    (1u << 1)
#define E1000_TCTL_PSP   (1u << 3)

#define E1000_RX_STATUS_DD 0x01u
#define E1000_RX_STATUS_EOP 0x02u

#define E1000_TX_CMD_EOP  0x01u
#define E1000_TX_CMD_IFCS 0x02u
#define E1000_TX_CMD_RS   0x08u
#define E1000_TX_STATUS_DD 0x01u

#define E1000_RX_DESC_COUNT 32u
#define E1000_TX_DESC_COUNT 8u
#define E1000_BUFFER_SIZE   2048u

#define PCI_CLASS_NETWORK 0x02u
#define PCI_SUBCLASS_ETHERNET 0x00u

#pragma pack(push, 1)
typedef struct
{
    u64 address;
    u16 length;
    u16 checksum;
    u8 status;
    u8 errors;
    u16 special;
} E1000RxDesc;

typedef struct
{
    u64 address;
    u16 length;
    u8 cso;
    u8 command;
    u8 status;
    u8 css;
    u16 special;
} E1000TxDesc;
#pragma pack(pop)

static E1000RxDesc rx_desc[E1000_RX_DESC_COUNT] __attribute__((aligned(16)));
static E1000TxDesc tx_desc[E1000_TX_DESC_COUNT] __attribute__((aligned(16)));
static u8 rx_buffers[E1000_RX_DESC_COUNT][E1000_BUFFER_SIZE] __attribute__((aligned(16)));
static u8 tx_buffers[E1000_TX_DESC_COUNT][E1000_BUFFER_SIZE] __attribute__((aligned(16)));

static bool adapter_present = false;
static bool adapter_ready = false;
static PciDevice adapter_pci;
static volatile u32* mmio = NULL;
static u8 adapter_mac[6];
static u32 rx_index = 0u;
static u32 tx_index = 0u;
static const char* last_error_text = "not detected";

static u32 reg_read(u32 offset)
{
    if (mmio == NULL)
        return 0u;

    return mmio[offset / 4u];
}

static void reg_write(u32 offset, u32 value)
{
    if (mmio == NULL)
        return;

    mmio[offset / 4u] = value;
    __asm__ volatile ("" ::: "memory");
}

static bool mac_is_valid(void)
{
    bool any = false;
    bool all_ff = true;

    for (u32 i = 0u; i < 6u; i++)
    {
        if (adapter_mac[i] != 0u)
            any = true;
        if (adapter_mac[i] != 0xFFu)
            all_ff = false;
    }

    return any && !all_ff;
}

static void read_mac(void)
{
    u32 low = reg_read(E1000_REG_RAL0);
    u32 high = reg_read(E1000_REG_RAH0);

    adapter_mac[0] = (u8)(low & 0xFFu);
    adapter_mac[1] = (u8)((low >> 8) & 0xFFu);
    adapter_mac[2] = (u8)((low >> 16) & 0xFFu);
    adapter_mac[3] = (u8)((low >> 24) & 0xFFu);
    adapter_mac[4] = (u8)(high & 0xFFu);
    adapter_mac[5] = (u8)((high >> 8) & 0xFFu);
}

static bool setup_rings(void)
{
    root_memzero(rx_desc, sizeof(rx_desc));
    root_memzero(tx_desc, sizeof(tx_desc));

    for (u32 i = 0u; i < E1000_RX_DESC_COUNT; i++)
    {
        rx_desc[i].address = (u64)(usize)&rx_buffers[i][0];
        rx_desc[i].status = 0u;
    }

    for (u32 i = 0u; i < E1000_TX_DESC_COUNT; i++)
    {
        tx_desc[i].address = (u64)(usize)&tx_buffers[i][0];
        tx_desc[i].status = E1000_TX_STATUS_DD;
    }

    u64 rx_base = (u64)(usize)&rx_desc[0];
    u64 tx_base = (u64)(usize)&tx_desc[0];

    reg_write(E1000_REG_RDBAL, (u32)(rx_base & 0xFFFFFFFFull));
    reg_write(E1000_REG_RDBAH, (u32)(rx_base >> 32));
    reg_write(E1000_REG_RDLEN, (u32)sizeof(rx_desc));
    reg_write(E1000_REG_RDH, 0u);
    reg_write(E1000_REG_RDT, E1000_RX_DESC_COUNT - 1u);

    reg_write(E1000_REG_TDBAL, (u32)(tx_base & 0xFFFFFFFFull));
    reg_write(E1000_REG_TDBAH, (u32)(tx_base >> 32));
    reg_write(E1000_REG_TDLEN, (u32)sizeof(tx_desc));
    reg_write(E1000_REG_TDH, 0u);
    reg_write(E1000_REG_TDT, 0u);

    rx_index = 0u;
    tx_index = 0u;

    reg_write(E1000_REG_RCTL,
        E1000_RCTL_EN |
        E1000_RCTL_BAM |
        E1000_RCTL_SECRC);

    reg_write(E1000_REG_TCTL,
        E1000_TCTL_EN |
        E1000_TCTL_PSP |
        (0x10u << 4) |
        (0x40u << 12));

    reg_write(E1000_REG_TIPG,
        10u |
        (8u << 10) |
        (6u << 20));

    return true;
}

static bool e1000_driver_probe(const RootDevice* device)
{
    if (device == NULL || device->bus != ROOT_DEVICE_BUS_PCI)
        return false;

    if (
        device->pci.vendor_id != E1000_VENDOR_INTEL ||
        device->pci.device_id != E1000_DEVICE_QEMU ||
        device->pci.class_code != PCI_CLASS_NETWORK ||
        device->pci.subclass != PCI_SUBCLASS_ETHERNET
    )
    {
        return false;
    }

    if (
        device->pci.bar_count == 0u ||
        !device->pci.bars[0].present ||
        device->pci.bars[0].type == PCI_BAR_IO ||
        device->pci.bars[0].base > 0xFFFFFFFFull
    )
    {
        return false;
    }

    return true;
}

static bool e1000_driver_attach(RootDevice* device)
{
    if (!e1000_driver_probe(device))
        return false;

    if (adapter_present)
        return true;

    adapter_present = true;
    adapter_ready = false;
    adapter_pci = device->pci;
    mmio = (volatile u32*)(usize)device->pci.bars[0].base;
    last_error_text = "initializing";

    if (!pci_enable_memory(device->pci.address))
    {
        last_error_text = "PCI memory enable failed";
        return false;
    }

    if (!pci_enable_bus_mastering(device->pci.address))
    {
        last_error_text = "PCI bus mastering failed";
        return false;
    }

    reg_write(E1000_REG_IMC, 0xFFFFFFFFu);
    (void)reg_read(E1000_REG_ICR);

    u32 ctrl = reg_read(E1000_REG_CTRL);
    reg_write(E1000_REG_CTRL, ctrl | E1000_CTRL_SLU);

    read_mac();
    if (!mac_is_valid())
    {
        last_error_text = "invalid MAC address";
        return false;
    }

    if (!setup_rings())
    {
        last_error_text = "ring setup failed";
        return false;
    }

    adapter_ready = true;
    last_error_text = "none";
    return true;
}

static void e1000_driver_detach(RootDevice* device)
{
    (void)device;
    adapter_present = false;
    adapter_ready = false;
    mmio = NULL;
    root_memzero(&adapter_pci, sizeof(adapter_pci));
    root_memzero(adapter_mac, sizeof(adapter_mac));
    last_error_text = "detached";
}

static const RootDriver e1000_root_driver =
{
    .name = "e1000",
    .match =
    {
        .vendor_id = E1000_VENDOR_INTEL,
        .device_id = E1000_DEVICE_QEMU,
        .class_code = PCI_CLASS_NETWORK,
        .subclass = PCI_SUBCLASS_ETHERNET,
        .prog_if = ROOT_DRIVER_ANY_CLASS
    },
    .probe = e1000_driver_probe,
    .attach = e1000_driver_attach,
    .detach = e1000_driver_detach
};

void e1000_init(void)
{
    adapter_present = false;
    adapter_ready = false;
    mmio = NULL;
    root_memzero(&adapter_pci, sizeof(adapter_pci));
    root_memzero(adapter_mac, sizeof(adapter_mac));
    last_error_text = "not detected";

    (void)device_manager_register_driver(&e1000_root_driver);
    device_manager_bind_drivers();
}

bool e1000_ready(void)
{
    return adapter_ready;
}

bool e1000_link_up(void)
{
    return adapter_ready && ((reg_read(E1000_REG_STATUS) & (1u << 1)) != 0u);
}

bool e1000_send_frame(const void* data, usize size)
{
    if (!adapter_ready || data == NULL || size == 0u || size > E1000_BUFFER_SIZE)
        return false;

    E1000TxDesc* descriptor = &tx_desc[tx_index];

    if ((descriptor->status & E1000_TX_STATUS_DD) == 0u)
        return false;

    root_memcpy(&tx_buffers[tx_index][0], data, size);

    descriptor->length = (u16)size;
    descriptor->cso = 0u;
    descriptor->command = E1000_TX_CMD_EOP | E1000_TX_CMD_IFCS | E1000_TX_CMD_RS;
    descriptor->status = 0u;
    descriptor->css = 0u;
    descriptor->special = 0u;

    u32 current = tx_index;
    tx_index = (tx_index + 1u) % E1000_TX_DESC_COUNT;
    reg_write(E1000_REG_TDT, tx_index);

    /* Poll only a bounded number of times. */
    for (u32 spin = 0u; spin < 100000u; spin++)
    {
        if ((tx_desc[current].status & E1000_TX_STATUS_DD) != 0u)
            return true;
        __asm__ volatile ("pause");
    }

    last_error_text = "TX completion timeout";
    return false;
}

bool e1000_receive_frame(void* output, usize capacity, usize* result_size)
{
    if (result_size != NULL)
        *result_size = 0u;

    if (!adapter_ready || output == NULL || capacity == 0u)
        return false;

    E1000RxDesc* descriptor = &rx_desc[rx_index];
    if ((descriptor->status & E1000_RX_STATUS_DD) == 0u)
        return false;

    usize length = descriptor->length;
    bool complete = (descriptor->status & E1000_RX_STATUS_EOP) != 0u;

    if (length > capacity)
        length = capacity;

    if (descriptor->errors == 0u && complete && length > 0u)
        root_memcpy(output, &rx_buffers[rx_index][0], length);
    else
        length = 0u;

    descriptor->status = 0u;
    descriptor->errors = 0u;
    descriptor->length = 0u;

    u32 released = rx_index;
    rx_index = (rx_index + 1u) % E1000_RX_DESC_COUNT;
    reg_write(E1000_REG_RDT, released);

    if (result_size != NULL)
        *result_size = length;

    return length > 0u;
}

void e1000_get_mac(u8 output[6])
{
    if (output == NULL)
        return;
    root_memcpy(output, adapter_mac, 6u);
}

const PciDevice* e1000_pci_device(void)
{
    return adapter_present ? &adapter_pci : NULL;
}

const char* e1000_last_error(void)
{
    return last_error_text;
}
