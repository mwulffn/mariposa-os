/*
 * bcache.c - the block cache
 *
 * A pool of 512-byte buffers keyed by (device, block): found through a hash
 * table, kept in a list ordered by last use so that the one to replace is
 * always the tail - no scanning, which at a few thousand buffers would cost
 * more than the disk read being avoided. One mutex over all of it: the disk
 * is the bottleneck, not the lock.
 *
 * The buffers come from the allocator, not from .bss. They used to be a
 * static array, which fixed the size whatever the machine had, hid 128KB
 * from every memory report, and grew the kernel image far enough to land on
 * top of the test heaps.
 */
#include "bcache.h"
#include "blk.h"
#include "mem.h"
#include "task.h"

#define HASH_SIZE 256           /* a power of two */
#define NONE      0xFFFF

/* Links are indices, not pointers: half the size, and `b - bufs` is a
 * division by the struct size on a 68000. */
struct buf {
    const struct blkdev *dev;   /* NULL: holds nothing */
    unsigned long  lba;
    unsigned short self;
    unsigned short hash_next;
    unsigned short newer, older;        /* the use-ordered list */
    unsigned char  data[BLK_SIZE];
};

static struct buf *bufs;
static unsigned short hash[HASH_SIZE];
static unsigned short newest, oldest;
static struct mutex lock;

unsigned long bc_blocks;
unsigned long bc_hits, bc_misses;

static unsigned bucket(const struct blkdev *dev, unsigned long lba)
{
    return (unsigned)((lba ^ (lba >> 8) ^ ((unsigned long)dev >> 4)) & (HASH_SIZE - 1));
}

/* ------------------------------------------------------- the use-order list --- */

static void unlink_use(struct buf *b)
{
    if (b->newer != NONE) bufs[b->newer].older = b->older; else newest = b->older;
    if (b->older != NONE) bufs[b->older].newer = b->newer; else oldest = b->newer;
}

static void make_newest(struct buf *b)
{
    b->newer = NONE;
    b->older = newest;
    if (newest != NONE)
        bufs[newest].newer = b->self;
    else
        oldest = b->self;
    newest = b->self;
}

static void touch(struct buf *b)
{
    if (newest != b->self) {
        unlink_use(b);
        make_newest(b);
    }
}

/* ---------------------------------------------------------------- lookup --- */

static struct buf *lookup(const struct blkdev *dev, unsigned long lba)
{
    unsigned short i;

    if (!bufs)
        return 0;
    for (i = hash[bucket(dev, lba)]; i != NONE; i = bufs[i].hash_next)
        if (bufs[i].dev == dev && bufs[i].lba == lba) {
            touch(&bufs[i]);
            return &bufs[i];
        }
    return 0;
}

static void unhash(struct buf *b)
{
    unsigned short *link = &hash[bucket(b->dev, b->lba)];

    while (*link != NONE && *link != b->self)
        link = &bufs[*link].hash_next;
    if (*link == b->self)
        *link = b->hash_next;
    b->dev = 0;
}

/* Forget what a buffer holds and make it the first to be reused. */
static void discard(struct buf *b)
{
    unhash(b);
    unlink_use(b);
    b->older = NONE;
    b->newer = oldest;
    if (oldest != NONE)
        bufs[oldest].older = b->self;
    else
        newest = b->self;
    oldest = b->self;
}

/* A buffer for (dev, lba), which is not in the cache: the least recently
 * used one, whatever it held. Empty buffers are the oldest of all. */
static struct buf *claim(const struct blkdev *dev, unsigned long lba)
{
    struct buf *b = &bufs[oldest];
    unsigned h = bucket(dev, lba);

    if (b->dev)
        unhash(b);
    b->dev = dev;
    b->lba = lba;
    b->hash_next = hash[h];
    hash[h] = b->self;
    touch(b);
    return b;
}

/* blkcopy.s. Callers' buffers are nearly always even - block buffers are
 * allocated, and the allocator aligns to 8 - but nothing promises it. */
void blk_copy512(void *to, const void *from);

static void copy_block(unsigned char *to, const unsigned char *from)
{
    int i;

    if (!(((unsigned long)to | (unsigned long)from) & 1)) {
        blk_copy512(to, from);
        return;
    }
    for (i = 0; i < BLK_SIZE; i++)
        to[i] = from[i];
}

void bc_init(void)
{
    unsigned long want, i;

    for (i = 0; i < HASH_SIZE; i++)
        hash[i] = NONE;
    newest = oldest = NONE;
    bc_hits = bc_misses = 0;
    bc_blocks = 0;
    bufs = 0;

    /* An eighth of what is free. Fast RAM if there is any: the cache is
     * touched by the CPU and nothing else, and chip RAM is the scarce kind. */
    want = mem_avail(ALLOC_FAST);
    if (!want)
        want = mem_avail(ALLOC_SLOW);
    if (!want)
        want = mem_avail(ALLOC_CHIP);
    want = (want >> 3) / sizeof(struct buf);
    if (want < BC_MIN_BLOCKS) want = BC_MIN_BLOCKS;
    if (want > BC_MAX_BLOCKS) want = BC_MAX_BLOCKS;

    /* Take less rather than nothing. */
    while (want >= BC_MIN_BLOCKS && !bufs) {
        bufs = mem_alloc(want * sizeof(struct buf), ALLOC_ANY);
        if (!bufs)
            want >>= 1;
    }
    if (!bufs)
        return;                         /* off: straight through to the disk */

    bc_blocks = want;
    for (i = 0; i < want; i++) {
        bufs[i].dev = 0;
        bufs[i].self = (unsigned short)i;
        make_newest(&bufs[i]);
    }
}

int bc_read(const struct blkdev *dev, unsigned long lba, unsigned long count, void *buf)
{
    unsigned char *out = buf;
    int rc = 0;

    if (!bufs)
        return blk_read(dev, lba, count, buf);

    mutex_lock(&lock);
    while (count && rc == 0) {
        struct buf *b = lookup(dev, lba);
        unsigned long run, i;

        if (b) {
            bc_hits++;
            copy_block(out, b->data);
            lba++;
            count--;
            out += BLK_SIZE;
            continue;
        }

        /* How far does the miss run? That many blocks in one command:
         * straight into the caller's buffer, then copied into the cache.
         * The command is what costs, not the sector. */
        for (run = 1; run < count && run < 256 && !lookup(dev, lba + run); run++)
            ;
        bc_misses += run;
        rc = blk_read(dev, lba, run, out);
        for (i = 0; i < run && rc == 0; i++)
            copy_block(claim(dev, lba + i)->data, out + (i << 9));
        lba += run;
        count -= run;
        out += run << 9;
    }
    mutex_unlock(&lock);
    return rc;
}

int bc_write(const struct blkdev *dev, unsigned long lba, unsigned long count, const void *buf)
{
    const unsigned char *in = buf;
    unsigned long i;
    int rc;

    if (!bufs)
        return blk_write(dev, lba, count, buf);

    mutex_lock(&lock);
    /* The disk first. If it fails the cache must not claim otherwise, so
     * whatever it held for these blocks is dropped. */
    rc = blk_write(dev, lba, count, buf);
    for (i = 0; i < count; i++) {
        struct buf *b = lookup(dev, lba + i);

        if (rc != 0) {
            if (b)
                discard(b);
            continue;
        }
        if (!b)
            b = claim(dev, lba + i);
        copy_block(b->data, in + (i << 9));
    }
    mutex_unlock(&lock);
    return rc;
}

int bc_read_direct(const struct blkdev *dev, unsigned long lba, unsigned long count, void *buf)
{
    return blk_read(dev, lba, count, buf);
}

int bc_write_direct(const struct blkdev *dev, unsigned long lba, unsigned long count, const void *buf)
{
    unsigned long i;
    int rc;

    if (!bufs)
        return blk_write(dev, lba, count, buf);

    mutex_lock(&lock);
    rc = blk_write(dev, lba, count, buf);
    /* Whether it worked or not: what the cache holds for these blocks can
     * no longer be vouched for. Nearly always nothing - bulk data does not
     * enter the cache - so nearly always a run of hash misses. */
    for (i = 0; i < count; i++) {
        unsigned short n;

        for (n = hash[bucket(dev, lba + i)]; n != NONE; n = bufs[n].hash_next)
            if (bufs[n].dev == dev && bufs[n].lba == lba + i) {
                discard(&bufs[n]);
                break;
            }
    }
    mutex_unlock(&lock);
    return rc;
}

void bc_forget(const struct blkdev *dev)
{
    int i;

    mutex_lock(&lock);
    for (i = 0; i < (int)bc_blocks; i++)
        if (bufs[i].dev == dev)
            discard(&bufs[i]);
    mutex_unlock(&lock);
}
