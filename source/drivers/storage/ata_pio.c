#include "ata_pio.h"
#include "io.h"

#define ATA_PRIMARY_DATA       0x1F0
#define ATA_PRIMARY_ERROR      0x1F1
#define ATA_PRIMARY_SECCOUNT   0x1F2
#define ATA_PRIMARY_LBA0       0x1F3
#define ATA_PRIMARY_LBA1       0x1F4
#define ATA_PRIMARY_LBA2       0x1F5
#define ATA_PRIMARY_DRIVE      0x1F6
#define ATA_PRIMARY_STATUS     0x1F7
#define ATA_PRIMARY_COMMAND    0x1F7
#define ATA_PRIMARY_ALTSTATUS  0x3F6

#define ATA_STATUS_ERR 0x01
#define ATA_STATUS_DRQ 0x08
#define ATA_STATUS_DF  0x20
#define ATA_STATUS_BSY 0x80

#define ATA_CMD_READ_SECTORS   0x20
#define ATA_CMD_WRITE_SECTORS  0x30
#define ATA_CMD_CACHE_FLUSH    0xE7
#define ATA_CMD_IDENTIFY       0xEC

#define ATA_TIMEOUT 1000000u
#define ATA_LBA28_MAX 0x0FFFFFFFu

static void ata_delay_400ns(void)
{
    (void)inb(ATA_PRIMARY_ALTSTATUS);
    (void)inb(ATA_PRIMARY_ALTSTATUS);
    (void)inb(ATA_PRIMARY_ALTSTATUS);
    (void)inb(ATA_PRIMARY_ALTSTATUS);
}

static bool ata_wait_not_busy(void)
{
    for (u32 i = 0; i < ATA_TIMEOUT; i++)
    {
        u8 status = inb(ATA_PRIMARY_STATUS);

        if ((status & ATA_STATUS_BSY) == 0)
        {
            return true;
        }
    }

    return false;
}

static bool ata_wait_drq(void)
{
    for (u32 i = 0; i < ATA_TIMEOUT; i++)
    {
        u8 status = inb(ATA_PRIMARY_STATUS);

        if (status & (ATA_STATUS_ERR | ATA_STATUS_DF))
        {
            return false;
        }

        if (
            (status & ATA_STATUS_BSY) == 0
            &&
            (status & ATA_STATUS_DRQ) != 0
        )
        {
            return true;
        }
    }

    return false;
}

static void ata_select_lba28(u32 lba)
{
    outb(
        ATA_PRIMARY_DRIVE,
        (u8)(0xE0u | ((lba >> 24) & 0x0Fu))
    );

    ata_delay_400ns();
}

bool ata_pio_init(AtaPioDevice* device)
{
    if (device == NULL)
    {
        return false;
    }

    device->present = false;
    device->sector_count = 0;

    outb(ATA_PRIMARY_DRIVE, 0xA0);
    ata_delay_400ns();

    outb(ATA_PRIMARY_SECCOUNT, 0);
    outb(ATA_PRIMARY_LBA0, 0);
    outb(ATA_PRIMARY_LBA1, 0);
    outb(ATA_PRIMARY_LBA2, 0);
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_IDENTIFY);

    u8 status = inb(ATA_PRIMARY_STATUS);

    if (status == 0)
    {
        return false;
    }

    if (!ata_wait_not_busy())
    {
        return false;
    }

    /*
     * IDENTIFY on an ATA disk leaves LBA1/LBA2 at zero.
     * Non-zero values normally indicate ATAPI/SATA signatures that this
     * small PIO driver is not intended to operate yet.
     */
    if (
        inb(ATA_PRIMARY_LBA1) != 0
        ||
        inb(ATA_PRIMARY_LBA2) != 0
    )
    {
        return false;
    }

    if (!ata_wait_drq())
    {
        return false;
    }

    u16 identify[256];

    for (u32 i = 0; i < 256; i++)
    {
        identify[i] = inw(ATA_PRIMARY_DATA);
    }

    u32 sectors =
        (u32)identify[60]
        |
        ((u32)identify[61] << 16);

    if (sectors == 0)
    {
        return false;
    }

    device->present = true;
    device->sector_count = sectors;

    return true;
}

bool ata_pio_read_sector(
    const AtaPioDevice* device,
    u32 lba,
    void* buffer
)
{
    if (
        device == NULL
        ||
        !device->present
        ||
        buffer == NULL
        ||
        lba >= device->sector_count
        ||
        lba > ATA_LBA28_MAX
    )
    {
        return false;
    }

    if (!ata_wait_not_busy())
    {
        return false;
    }

    ata_select_lba28(lba);

    outb(ATA_PRIMARY_SECCOUNT, 1);
    outb(ATA_PRIMARY_LBA0, (u8)(lba & 0xFFu));
    outb(ATA_PRIMARY_LBA1, (u8)((lba >> 8) & 0xFFu));
    outb(ATA_PRIMARY_LBA2, (u8)((lba >> 16) & 0xFFu));
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_READ_SECTORS);

    if (!ata_wait_drq())
    {
        return false;
    }

    u8* bytes = (u8*)buffer;

    for (u32 i = 0; i < 256; i++)
    {
        u16 word = inw(ATA_PRIMARY_DATA);

        bytes[i * 2] = (u8)(word & 0xFFu);
        bytes[i * 2 + 1] = (u8)(word >> 8);
    }

    ata_delay_400ns();
    return true;
}

bool ata_pio_write_sector(
    const AtaPioDevice* device,
    u32 lba,
    const void* buffer
)
{
    if (
        device == NULL
        ||
        !device->present
        ||
        buffer == NULL
        ||
        lba >= device->sector_count
        ||
        lba > ATA_LBA28_MAX
    )
    {
        return false;
    }

    if (!ata_wait_not_busy())
    {
        return false;
    }

    ata_select_lba28(lba);

    outb(ATA_PRIMARY_SECCOUNT, 1);
    outb(ATA_PRIMARY_LBA0, (u8)(lba & 0xFFu));
    outb(ATA_PRIMARY_LBA1, (u8)((lba >> 8) & 0xFFu));
    outb(ATA_PRIMARY_LBA2, (u8)((lba >> 16) & 0xFFu));
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_WRITE_SECTORS);

    if (!ata_wait_drq())
    {
        return false;
    }

    const u8* bytes = (const u8*)buffer;

    for (u32 i = 0; i < 256; i++)
    {
        u16 word =
            (u16)bytes[i * 2]
            |
            ((u16)bytes[i * 2 + 1] << 8);

        outw(ATA_PRIMARY_DATA, word);
    }

    outb(ATA_PRIMARY_COMMAND, ATA_CMD_CACHE_FLUSH);

    if (!ata_wait_not_busy())
    {
        return false;
    }

    u8 status = inb(ATA_PRIMARY_STATUS);

    return
        (status & (ATA_STATUS_ERR | ATA_STATUS_DF)) == 0;
}
