/*
 * rdb.h - Amiga Rigid Disk Block parsing
 *
 * Parsing only. Nothing here prints, and nothing here knows what a boot is
 * for: it reads blocks through a struct blkdev and hands back structs. The
 * ROM decides what to report and the kernel will decide differently, which
 * is the same mechanism-versus-policy line drawn in serial_hw.h.
 */
#ifndef RDB_H
#define RDB_H

#include "blkdev.h"

#define RDB_BLOCK_BYTES   512
#define RDB_SCAN_BLOCKS   16          /* the RDB must live in blocks 0-15 */
#define RDB_NAME_MAX      31          /* BCPL string, one length byte */

#define RDB_END           0xFFFFFFFFUL  /* end of a block list */

/* Return codes. Distinguished so a caller can say which went wrong. */
#define RDB_OK             0
#define RDB_NOT_FOUND     (-1)
#define RDB_READ_ERROR    (-2)
#define RDB_BAD_MAGIC     (-3)

struct rdb_info {
    unsigned long block_bytes;
    unsigned long cylinders;
    unsigned long heads;
    unsigned long sectors;
    unsigned long partlist;       /* block of the first PART, or RDB_END */
    unsigned long found_block;    /* where the RDB was, or where a read failed */
};

struct rdb_partition {
    char          name[RDB_NAME_MAX + 1];
    unsigned long low_cyl;
    unsigned long high_cyl;
    unsigned long dostype;
    unsigned long next;           /* next PART block, or RDB_END */
    unsigned long start_lba;      /* derived from the geometry */
    unsigned long sectors;        /* derived from the geometry */
};

/*
 * Scan blocks 0..15 for "RDSK". The matching block is left in `buf`, which
 * must be at least RDB_BLOCK_BYTES and word aligned; callers that go on to
 * read partitions need the geometry that stays in `info`.
 */
int rdb_find(const struct blkdev *dev, void *buf, struct rdb_info *info);

/*
 * Read the PART block at `lba` and fill in `part`, deriving start_lba and
 * sectors from the RDB's geometry. `buf` is scratch, not the RDB's buffer.
 */
int rdb_read_partition(const struct blkdev *dev, const struct rdb_info *info,
                       unsigned long lba, void *buf,
                       struct rdb_partition *part);

#endif /* RDB_H */
