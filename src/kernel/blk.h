/*
 * blk.h - block devices and partitions
 *
 * docs/fs_design.md. A block device is the shared struct blkdev, registered
 * as a DEV_BLOCK device with the blkdev as its hw pointer. A partition is a
 * block device too: a window onto its parent.
 */
#ifndef BLK_H
#define BLK_H

#include "blkdev.h"
#include "dev.h"

#define BLK_SIZE        512
#define BLK_MAX_PARTS   8

struct blk_partition {
    struct blkdev        bd;        /* what everyone else sees */
    struct device        dev;
    const struct blkdev *parent;
    unsigned long        start;     /* first block, on the parent */
    unsigned long        dostype;   /* from the RDB: what the partition says
                                     * it holds. A hint, never trusted. */
    char                 name[12];      /* "ide0p1" */
    char                 label[32];     /* the RDB's own name: "DH0" */
};

/* Find the disks, read their partition tables, register everything.
 * Returns how many block devices there now are. Task context or boot. */
int blk_init(void);

/* By registry name. NULL if there is none or it is not a block device. */
const struct blkdev *blk_find(const char *name);

/* The partition behind a block device, or NULL if it is a whole disk. */
const struct blk_partition *blk_partition_of(const struct blkdev *dev);

/* Checked against the device's size, which dev->read alone is not. */
int blk_read(const struct blkdev *dev, unsigned long lba, unsigned long count, void *buf);
int blk_write(const struct blkdev *dev, unsigned long lba, unsigned long count, const void *buf);

/* ide.c: the Gayle IDE port. NULL if no drive answers. */
struct blkdev *ide_init(void);

#endif /* BLK_H */
