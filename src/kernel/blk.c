/*
 * blk.c - block devices and partitions
 */
#include "blk.h"
#include "rdb.h"
#include "kprintf.h"

static struct device disk_dev;
static struct blk_partition parts[BLK_MAX_PARTS];
static int nparts;
static unsigned char block_buf[BLK_SIZE];

/* ----------------------------------------------------------- partitions --- */

static struct blk_partition *as_partition(const struct blkdev *dev)
{
    int i;

    for (i = 0; i < nparts; i++)
        if (dev == &parts[i].bd)
            return &parts[i];
    return 0;
}

const struct blk_partition *blk_partition_of(const struct blkdev *dev)
{
    return as_partition(dev);
}

/* A partition is a window: block 0 is wherever the RDB says, and the end is
 * the end. Past it is the next partition's data, and handing that over - or
 * writing on it - is the worst thing this layer could do. */
static int part_read(const struct blkdev *dev, unsigned long lba,
                     unsigned count, void *buf)
{
    const struct blk_partition *p = as_partition(dev);

    if (!p || lba >= dev->blocks || count > dev->blocks - lba)
        return -1;
    return p->parent->read(p->parent, p->start + lba, count, buf);
}

static int part_write(const struct blkdev *dev, unsigned long lba,
                      unsigned count, const void *buf)
{
    const struct blk_partition *p = as_partition(dev);

    if (!p || !p->parent->write || lba >= dev->blocks || count > dev->blocks - lba)
        return -1;
    return p->parent->write(p->parent, p->start + lba, count, buf);
}

static int part_present(const struct blkdev *dev)
{
    const struct blk_partition *p = as_partition(dev);

    return p ? p->parent->present(p->parent) : 0;
}

static void name_partition(struct blk_partition *p, const char *disk, int n)
{
    char *out = p->name;

    while (*disk)
        *out++ = *disk++;
    *out++ = 'p';
    *out++ = (char)('0' + n);
    *out = '\0';
}

/*
 * Walk the RDB's partition list. RDB_PARTLIST says where the first PART
 * block is and each PART_NEXT where the next is. The ROM's boot path reads
 * LBA 1 and stops - good enough to boot from the first partition of a disk
 * laid out the usual way, and not something to inherit.
 */
static void scan_partitions(const struct blkdev *disk)
{
    struct rdb_info rdb;
    struct rdb_partition rp;
    unsigned long at;
    int i;

    if (rdb_find(disk, block_buf, &rdb) != RDB_OK)
        return;

    for (at = rdb.partlist; at != RDB_END && nparts < BLK_MAX_PARTS; at = rp.next) {
        struct blk_partition *p = &parts[nparts];

        if (rdb_read_partition(disk, &rdb, at, block_buf, &rp) != RDB_OK)
            break;                      /* a broken chain ends the list */
        if (disk->blocks && (rp.start_lba >= disk->blocks ||
                             rp.sectors > disk->blocks - rp.start_lba)) {
            pr_warn("%s: partition %s runs past the end of the disk - ignored\n",
                    disk->name, rp.name);
            continue;
        }

        name_partition(p, disk->name, nparts);
        for (i = 0; i < 31 && rp.name[i]; i++)
            p->label[i] = rp.name[i];
        p->label[i] = '\0';
        p->parent  = disk;
        p->start   = rp.start_lba;
        p->dostype = rp.dostype;

        p->bd.name    = p->name;
        p->bd.read    = part_read;
        p->bd.present = part_present;
        p->bd.hw      = 0;
        p->bd.write   = disk->write ? part_write : 0;
        p->bd.blocks  = rp.sectors;

        p->dev.name  = p->name;
        p->dev.class = DEV_BLOCK;
        p->dev.ops   = 0;
        p->dev.hw    = &p->bd;
        dev_register(&p->dev);
        nparts++;
    }
}

/* ----------------------------------------------------------------- setup --- */

int blk_init(void)
{
    struct blkdev *disk = ide_init();

    nparts = 0;
    if (!disk)
        return 0;

    disk_dev.name  = disk->name;
    disk_dev.class = DEV_BLOCK;
    disk_dev.ops   = 0;
    disk_dev.hw    = disk;
    dev_register(&disk_dev);

    scan_partitions(disk);
    return nparts + 1;
}

const struct blkdev *blk_find(const char *name)
{
    struct device *d = dev_find(name);

    return (d && d->class == DEV_BLOCK) ? (const struct blkdev *)d->hw : 0;
}

/* ------------------------------------------------------------------ I/O --- */

int blk_read(const struct blkdev *dev, unsigned long lba, unsigned long count, void *buf)
{
    unsigned char *out = buf;

    if (dev->blocks && (lba >= dev->blocks || count > dev->blocks - lba))
        return -1;
    while (count) {                     /* an ATA command moves at most 256 */
        unsigned n = count > 256 ? 256 : (unsigned)count;

        if (dev->read(dev, lba, n, out) != 0)
            return -1;
        lba += n;
        count -= n;
        out += (unsigned long)n << 9;
    }
    return 0;
}

int blk_write(const struct blkdev *dev, unsigned long lba, unsigned long count, const void *buf)
{
    const unsigned char *in = buf;

    if (!dev->write || (dev->blocks && (lba >= dev->blocks || count > dev->blocks - lba)))
        return -1;
    while (count) {
        unsigned n = count > 256 ? 256 : (unsigned)count;

        if (dev->write(dev, lba, n, in) != 0)
            return -1;
        lba += n;
        count -= n;
        in += (unsigned long)n << 9;
    }
    return 0;
}
