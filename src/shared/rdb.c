/*
 * rdb.c - Rigid Disk Block parsing
 *
 * Transcribed from the assembly partition.s. See rdb.h for what is
 * deliberately absent: any output at all.
 */
#include "rdb.h"

#define RDB_MAGIC   0x5244534BUL      /* "RDSK" */
#define PART_MAGIC  0x50415254UL      /* "PART" */

/* RDB field offsets */
#define RDB_ID          0
#define RDB_BLOCKBYTES  16
#define RDB_PARTLIST    28
#define RDB_CYLINDERS   64
#define RDB_SECTORS     68
#define RDB_HEADS       72

/* PART field offsets */
#define PART_ID         0
#define PART_NEXT       16
#define PART_DRIVENAME  36
#define PART_DOSENVVEC  128

/* DosEnvVec offsets, relative to PART_DOSENVVEC */
#define DE_LOWCYL       36
#define DE_HIGHCYL      40
#define DE_DOSTYPE      64

/* RDB fields are big-endian longwords at aligned offsets, and so is the
 * 68000 this only ever runs on, so a direct load is the whole conversion. */
static unsigned long be32(const void *buf, unsigned off)
{
    return *(const unsigned long *)((const unsigned char *)buf + off);
}

int rdb_find(const struct blkdev *dev, void *buf, struct rdb_info *info)
{
    unsigned long block;

    for (block = 0; block < RDB_SCAN_BLOCKS; block++) {
        info->found_block = block;

        if (dev->read(dev, block, 1, buf) != 0)
            return RDB_READ_ERROR;

        if (be32(buf, RDB_ID) != RDB_MAGIC)
            continue;

        info->block_bytes = be32(buf, RDB_BLOCKBYTES);
        info->cylinders   = be32(buf, RDB_CYLINDERS);
        info->heads       = be32(buf, RDB_HEADS);
        info->sectors     = be32(buf, RDB_SECTORS);
        info->partlist    = be32(buf, RDB_PARTLIST);
        return RDB_OK;
    }

    return RDB_NOT_FOUND;
}

/* The drive name is a BCPL string: one length byte then the characters, no
 * terminator. */
static void copy_bcpl_name(const unsigned char *src, char *dst)
{
    unsigned len = src[0];
    unsigned i;

    if (len > RDB_NAME_MAX)
        len = RDB_NAME_MAX;

    for (i = 0; i < len; i++)
        dst[i] = (char)src[1 + i];
    dst[len] = '\0';
}

int rdb_read_partition(const struct blkdev *dev, const struct rdb_info *info,
                       unsigned long lba, void *buf,
                       struct rdb_partition *part)
{
    unsigned long cylinders;

    if (dev->read(dev, lba, 1, buf) != 0)
        return RDB_READ_ERROR;

    if (be32(buf, PART_ID) != PART_MAGIC)
        return RDB_BAD_MAGIC;

    copy_bcpl_name((const unsigned char *)buf + PART_DRIVENAME, part->name);

    part->next     = be32(buf, PART_NEXT);
    part->low_cyl  = be32(buf, PART_DOSENVVEC + DE_LOWCYL);
    part->high_cyl = be32(buf, PART_DOSENVVEC + DE_HIGHCYL);
    part->dostype  = be32(buf, PART_DOSENVVEC + DE_DOSTYPE);

    /*
     * A full 32-bit multiply. The assembly needed mul32x16 here because
     * chained mulu.w is 16x16 and the LowCyl*Heads intermediate truncated,
     * putting any partition more than roughly 2GB in at the wrong LBA. vbcc
     * emits the three-mulu.w expansion, which does not have that problem.
     */
    cylinders = part->high_cyl - part->low_cyl + 1;
    part->start_lba = part->low_cyl * info->heads * info->sectors;
    part->sectors   = cylinders * info->heads * info->sectors;

    return RDB_OK;
}
