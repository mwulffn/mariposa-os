/*
 * bcache.h - the block cache
 *
 * docs/fs_design.md. Gayle's IDE is PIO with no DMA, so every word of every
 * sector goes through the CPU and a block read twice is a cost paid twice.
 * This is fast RAM spent to save the slowest thing in the machine.
 */
#ifndef BCACHE_H
#define BCACHE_H

#include "blkdev.h"

/*
 * How big: an eighth of whatever memory is free when bc_init() runs, fast
 * RAM for preference, between BC_MIN_BLOCKS and BC_MAX_BLOCKS. A 1MB machine
 * gets about 120KB of cache and an 8MB one about a megabyte - which is what
 * "spend fast RAM to save the disk" ought to mean, and what a fixed-size
 * static array could not.
 */
#define BC_MIN_BLOCKS  64
#define BC_MAX_BLOCKS  4096

/* After mem_init(). With no memory to be had the cache is simply off:
 * bc_read and bc_write still work, straight through to the disk. */
void bc_init(void);

extern unsigned long bc_blocks;     /* how many it got; 0 means off */

/* As blk_read/blk_write, through the cache. A run of blocks the cache does
 * not have is fetched with one command, not one each. Writes are
 * write-through: on the disk before this returns. Task context or boot. */
int bc_read(const struct blkdev *dev, unsigned long lba, unsigned long count, void *buf);
int bc_write(const struct blkdev *dev, unsigned long lba, unsigned long count, const void *buf);

/* Forget everything held for a device: it is going away, or something else
 * has written to it. */
void bc_forget(const struct blkdev *dev);

extern unsigned long bc_hits, bc_misses;

#endif /* BCACHE_H */
