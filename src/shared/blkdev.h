/*
 * blkdev.h - block device handle
 *
 * The seed of a device model, kept deliberately small. Today there is one
 * device and the ROM finds it by looking; eventually the kernel will want
 * something closer to a device tree, with devices discovered, named and
 * handed to whoever asks. The point of this file is that the layers above -
 * RDB parsing, FAT16, and the kernel's block I/O when it gets any - talk to
 * a handle rather than to Gayle directly, so growing into that costs a
 * rewrite of this header and nothing else.
 *
 * What it is NOT: an attempt to guess what that model looks like. No
 * registration, no reference counting, no partition children. Those go in
 * when something needs them.
 */
#ifndef BLKDEV_H
#define BLKDEV_H

struct blkdev {
    const char *name;

    /* Read `count` 512-byte blocks starting at `lba` into `buf`.
     * Returns 0 on success, non-zero on error. */
    int (*read)(const struct blkdev *dev, unsigned long lba,
                unsigned count, void *buf);

    /* Non-zero if the device answers. */
    int (*present)(const struct blkdev *dev);

    /* Transport state, owned by whoever built this struct. */
    const void *hw;

    /* The kernel's additions. The ROM's devices leave them zero: it never
     * writes, and it never asks how big a disk is. */

    /* Write `count` blocks. NULL on a device that cannot be written. */
    int (*write)(const struct blkdev *dev, unsigned long lba,
                 unsigned count, const void *buf);

    /* How many blocks there are, or 0 if unknown. */
    unsigned long blocks;
};

#endif /* BLKDEV_H */
