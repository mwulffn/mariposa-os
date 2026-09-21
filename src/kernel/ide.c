/*
 * ide.c - the Gayle IDE port
 *
 * Polled PIO over the ATA code the ROM also uses (src/shared/ata.c). What
 * the kernel adds is a lock: a transfer is a sequence of register writes
 * and a data loop, and two tasks interleaving theirs would corrupt both.
 * CRITICAL_ENTER is no answer - a 256-sector read is a long time to hold
 * off every interrupt in the machine - so this is what the sleeping mutex
 * was for.
 *
 * Polling is what it is for now. Sleeping on the drive's interrupt needs
 * Gayle's interrupt registers, and on a CF card, which answers at once,
 * buys little.
 */
#include "blk.h"
#include "ata.h"
#include "task.h"

static volatile unsigned char *gayle_reg(int n)
{
    return (volatile unsigned char *)(0x00DA0002UL + (unsigned long)n * 4UL);
}

static const struct ata_if gayle = {
    gayle_reg,
    (volatile unsigned short *)0x00DA0002UL
};

static struct mutex lock;
static unsigned short identify[256];

static int ide_read(const struct blkdev *dev, unsigned long lba,
                    unsigned count, void *buf)
{
    int rc;

    (void)dev;
    mutex_lock(&lock);
    rc = ata_read_sectors(&gayle, lba, count, buf);
    mutex_unlock(&lock);
    return rc;
}

static int ide_write(const struct blkdev *dev, unsigned long lba,
                     unsigned count, const void *buf)
{
    int rc;

    (void)dev;
    mutex_lock(&lock);
    rc = ata_write_sectors(&gayle, lba, count, buf);
    mutex_unlock(&lock);
    return rc;
}

static int ide_present(const struct blkdev *dev)
{
    (void)dev;
    return ata_present(&gayle);
}

static struct blkdev ide0 = { "ide0", ide_read, ide_present, &gayle, ide_write, 0 };

struct blkdev *ide_init(void)
{
    if (!ata_present(&gayle))
        return 0;
    mutex_lock(&lock);
    ide0.blocks = ata_capacity(&gayle, identify);
    mutex_unlock(&lock);
    return &ide0;
}
