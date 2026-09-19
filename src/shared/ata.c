/*
 * ata.c - LBA28 PIO reads, transport agnostic
 *
 * A transcription of the assembly ide.s, with the register addresses lifted
 * out into struct ata_if. See ata.h for why.
 */
#include "ata.h"

/* Bounded spin. The assembly used the same count; at 7MHz it is a little
 * over a second, which is long enough for a drive spinning up and short
 * enough that a dead one does not hang the boot. */
#define ATA_TIMEOUT 0x100000UL

#define ATA_CMD_READ    0x20
#define ATA_LBA_MASTER  0xE0

#define SECTOR_WORDS    256

unsigned char ata_status(const struct ata_if *ifc)
{
    return *ifc->reg(ATA_REG_STATUS);
}

unsigned char ata_error(const struct ata_if *ifc)
{
    return *ifc->reg(ATA_REG_ERROR);
}

int ata_present(const struct ata_if *ifc)
{
    return ata_status(ifc) != ATA_NO_DRIVE;
}

static int wait_not_busy(const struct ata_if *ifc)
{
    unsigned long spin;

    for (spin = ATA_TIMEOUT; spin != 0; spin--) {
        if (!(ata_status(ifc) & ATA_SR_BSY))
            return 0;
    }
    return -1;
}

static int wait_drq(const struct ata_if *ifc)
{
    unsigned long spin;

    for (spin = ATA_TIMEOUT; spin != 0; spin--) {
        unsigned char st = ata_status(ifc);

        if (st & ATA_SR_ERR)
            return -1;
        if (st & ATA_SR_DRQ)
            return 0;
    }
    return -1;
}

/*
 * The assembly put two NOPs after each task-file write for bus settling.
 * The address arithmetic around each volatile store here costs comparable
 * time, so they are not reproduced - but this is the one difference from
 * ide.s that neither the harness nor FS-UAE can show up, so it is the thing
 * to look at first if a real drive ever misbehaves.
 */
static void select_lba(const struct ata_if *ifc, unsigned long lba,
                       unsigned count)
{
    *ifc->reg(ATA_REG_LBA0)   = (unsigned char)(lba);
    *ifc->reg(ATA_REG_LBA1)   = (unsigned char)(lba >> 8);
    *ifc->reg(ATA_REG_LBA2)   = (unsigned char)(lba >> 16);
    *ifc->reg(ATA_REG_SELECT) = (unsigned char)(((lba >> 24) & 0x0F) |
                                                ATA_LBA_MASTER);
    /* 256 sectors is encoded as 0, which the truncation to a byte gives. */
    *ifc->reg(ATA_REG_NSECTOR) = (unsigned char)count;
}

int ata_read_sectors(const struct ata_if *ifc, unsigned long lba,
                     unsigned count, void *buf)
{
    unsigned short *out = (unsigned short *)buf;
    unsigned sector;

    if (count == 0 || count > ATA_MAX_SECTORS)
        return -1;

    if (wait_not_busy(ifc) != 0)
        return -1;

    select_lba(ifc, lba, count);
    *ifc->reg(ATA_REG_COMMAND) = ATA_CMD_READ;

    for (sector = 0; sector < count; sector++) {
        int word;

        if (wait_drq(ifc) != 0)
            return -1;

        /* A word read of the data port, so the two bytes land in disk order.
         * That is what lets a big-endian RDB magic compare and a
         * little-endian FAT field both come out of the same buffer. */
        for (word = 0; word < SECTOR_WORDS; word++)
            *out++ = *ifc->data;
    }

    if (wait_not_busy(ifc) != 0)
        return -1;

    if (ata_status(ifc) & ATA_SR_ERR)
        return -1;

    return 0;
}
