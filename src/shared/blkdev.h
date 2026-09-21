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

    /*
     * For file DATA, as opposed to a filesystem's own structures: the same
     * as read and write, but free to go around any cache. NULL means there
     * is nothing to go around - use read and write.
     *
     * Every Amiga still running boots from CompactFlash or an SSD. There is
     * no seek to save, so a cache hit on bulk data is worth only the
     * difference between a PIO transfer and a memory copy - about 2.5x -
     * while putting the data INTO a cache on the way past costs as much
     * again as fetching it. Metadata is the opposite case: small, read over
     * and over, and each time a whole disk command. So metadata goes
     * through the cache and bulk data goes straight between the disk and
     * the caller's memory, which with no MMU is simply a pointer.
     *
     * buf must be at an even address: the transfer is word moves.
     */
    int (*bulk_read)(const struct blkdev *dev, unsigned long lba,
                     unsigned count, void *buf);
    int (*bulk_write)(const struct blkdev *dev, unsigned long lba,
                      unsigned count, const void *buf);
};

#endif /* BLKDEV_H */
