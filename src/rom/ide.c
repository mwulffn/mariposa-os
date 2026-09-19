/*
 * ide.c - the ROM's boot block device
 *
 * Two things live here: Gayle's register mapping, and the one-entry device
 * table the boot path reads through. The ATA protocol itself is in
 * src/shared/ata.c and knows nothing about either.
 *
 * Gayle puts the task file at $DA0002 with four bytes between registers.
 * A CF card in the A600/A1200 PCMCIA slot would be the same protocol at
 * $A20000/$A30000; adding it means another `reg` function and another table
 * entry, and nothing in ata.c or above it changes. That is the whole reason
 * for the split - see docs/rom_scope_design.md.
 */
#include "ata.h"
#include "blkdev.h"
#include "rom.h"

/* Gayle IDE: register n at $DA0002 + n*4. */
static volatile unsigned char *gayle_reg(int n)
{
    return (volatile unsigned char *)(0x00DA0002UL + (unsigned long)n * 4UL);
}

static const struct ata_if gayle_ata = {
    gayle_reg,
    (volatile unsigned short *)0x00DA0002UL
};

static int ide_blk_read(const struct blkdev *dev, unsigned long lba,
                        unsigned count, void *buf)
{
    return ata_read_sectors((const struct ata_if *)dev->hw, lba, count, buf);
}

static int ide_blk_present(const struct blkdev *dev)
{
    return ata_present((const struct ata_if *)dev->hw);
}

/*
 * The device table. One entry today. When there is a second transport this
 * grows and blkdev_boot() probes in order rather than answering from memory.
 */
static const struct blkdev ide0 = {
    "ide0",
    ide_blk_read,
    ide_blk_present,
    &gayle_ata
};

const struct blkdev *blkdev_boot(void)
{
    return &ide0;
}

/* Called through ide_glue.s with the old register ABI. */
unsigned long rom_ide_read(void *buf, unsigned long lba, unsigned long count)
{
    const struct blkdev *dev = blkdev_boot();

    if (count > ATA_MAX_SECTORS)
        return (unsigned long)-1;
    if (dev->read(dev, lba, (unsigned)count, buf) != 0)
        return (unsigned long)-1;
    return 0;
}

/* Where ide_test_read drops its sector. */
#define IDE_TEST_DEST ((void *)0x30000UL)

unsigned long rom_ide_test_read(void)
{
    const struct blkdev *dev = blkdev_boot();
    unsigned long args[2];

    rom_serial_put_string("IDE: Reading sector 0...\r\n");

    if (!dev->present(dev)) {
        rom_serial_put_string("IDE: No drive (status=$7F)\r\n");
        return (unsigned long)-1;
    }

    if (dev->read(dev, 0, 1, IDE_TEST_DEST) != 0) {
        args[0] = ata_status(&gayle_ata);
        args[1] = ata_error(&gayle_ata);
        rom_printf("IDE: Error status=%x.b error=%x.b\r\n", args);
        return (unsigned long)-1;
    }

    args[0] = *(unsigned long *)IDE_TEST_DEST;
    rom_printf("IDE: Success! First bytes: %x.l\r\n", args);
    return 0;
}
