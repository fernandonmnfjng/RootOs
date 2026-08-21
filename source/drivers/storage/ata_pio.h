#ifndef ROOTOS_ATA_PIO_H
#define ROOTOS_ATA_PIO_H

#include "types.h"

#define ATA_PIO_SECTOR_SIZE 512u

typedef struct
{
    bool present;
    u32 sector_count;
} AtaPioDevice;

bool ata_pio_init(AtaPioDevice* device);

bool ata_pio_read_sector(
    const AtaPioDevice* device,
    u32 lba,
    void* buffer
);

bool ata_pio_write_sector(
    const AtaPioDevice* device,
    u32 lba,
    const void* buffer
);

#endif
