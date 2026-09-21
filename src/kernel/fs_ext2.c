/*
 * fs_ext2.c - ext2 behind the VFS
 *
 * The parsing is src/shared/ext2.c, which the ROM uses too. This is the
 * adapter; writing is added on top of it here, not there, because the ROM
 * never writes.
 */
#include "vfs.h"
#include "ext2.h"
#include "mem.h"
#include "kprintf.h"
#include "kstring.h"

struct ext2fs {
    struct ext2        fs;
    struct ext2_inode  inode;       /* the last one read: most calls come in
                                     * runs on the same file */
    struct ext2_dirent dirent;

    /* --- writing ---------------------------------------------------------
     * The superblock and the group descriptors are kept in memory and
     * written at sync: their free counts change with every block allocated,
     * and on a PIO disk three metadata writes per data block is two too
     * many. The bitmaps are what actually prevent a block being handed out
     * twice, and those are written at once. */
    unsigned char  sb[1024];
    unsigned char *gdt;             /* the whole table, gdt_blocks long */
    unsigned long  gdt_blocks;
    int            meta_dirty;      /* sb/gdt differ from the disk */
    int            marked_dirty;    /* the on-disk state says "not clean" */
    unsigned long  now;             /* there is no clock: the last write time
                                     * the volume knew, for new timestamps */

    /* Block buffers of the writer's own, so that the reader's are not
     * pulled out from under it: inode table, bitmap, data, two map levels. */
    unsigned char *ibuf, *bbuf, *dbuf, *m1, *m2;
    unsigned long  iblk;            /* which block ibuf holds */

    /* bbuf, m1 and m2 are small write-back caches: which block each holds,
     * and whether it has changed. A bitmap or a map block changes with every
     * block allocated, and writing it every time made each 1KB of data cost
     * about 4KB of PIO. They are flushed before the inode that depends on
     * them is written, so the order on disk - what crash safety rests on -
     * is what it was; there are just far fewer writes in it. */
    unsigned long  held[3];         /* [0] bbuf  [1] m1  [2] m2 */
    int            dirty[3];

    /* Where the last block came from. The next search starts there: a file
     * being written takes block after block from the same bitmap, and
     * starting from bit 0 each time meant re-reading everything already
     * taken - 13,000 cycles a block once a group was mostly full. It also
     * keeps a growing file's blocks adjacent, which is what lets them be
     * written in one command. */
    unsigned long  rotor_map;       /* the bitmap block it applies to */
    unsigned long  rotor_bit;
};

/* node.priv[0] is the inode number. */

static int to_vfs(int rc)
{
    switch (rc) {
        case EXT2_OK:          return VFS_OK;
        case EXT2_NOT_FOUND:   return VFS_ENOENT;
        case EXT2_NOT_EXT2:
        case EXT2_UNSUPPORTED: return VFS_ENODEV;
        default:               return VFS_EIO;
    }
}

static int writer_init(struct ext2fs *e);

static int ext2fs_probe(const struct blkdev *dev)
{
    static unsigned char sb[1024];
    struct ext2 fs;
    int rc = ext2_mount(dev, &fs, sb, sizeof sb);

    /* NEED_MEMORY means it got as far as believing the superblock. */
    return rc == EXT2_OK || rc == EXT2_NEED_MEMORY;
}

static int ext2fs_mount(const struct blkdev *dev, void **fsdata)
{
    struct ext2fs *e = mem_alloc(sizeof *e, ALLOC_ANY);
    unsigned char first[1024], *work;
    int rc;

    if (!e)
        return VFS_EIO;
    rc = ext2_mount(dev, &e->fs, first, sizeof first);
    if (rc == EXT2_NEED_MEMORY || rc == EXT2_OK) {
        work = mem_alloc(3 * e->fs.block_size, ALLOC_ANY);
        rc = work ? ext2_mount(dev, &e->fs, work, 3 * e->fs.block_size)
                  : EXT2_READ_ERROR;
        if (rc != EXT2_OK && work)
            mem_free(work);
    }
    if (rc != EXT2_OK) {
        mem_free(e);
        return to_vfs(rc);
    }
    e->inode.ino = 0;
    rc = writer_init(e);
    if (rc != VFS_OK) {
        mem_free(e->fs.buf[0]);
        mem_free(e);
        return rc;
    }
    *fsdata = e;
    return VFS_OK;
}

static void ext2fs_unmount(void *fsdata)
{
    struct ext2fs *e = fsdata;

    mem_free(e->gdt);
    mem_free(e->ibuf);
    mem_free(e->fs.buf[0]);
    mem_free(e);
}

static int get_inode(struct ext2fs *e, unsigned long ino)
{
    int rc;

    if (e->inode.ino == ino)
        return EXT2_OK;
    e->inode.ino = 0;
    rc = ext2_read_inode(&e->fs, ino, &e->inode);
    if (rc != EXT2_OK)
        e->inode.ino = 0;
    return rc;
}

static unsigned long type_of(unsigned short mode)
{
    switch (mode & EXT2_S_IFMT) {
        case EXT2_S_IFDIR: return VFS_DIR;
        case EXT2_S_IFREG: return VFS_FILE;
        default:           return VFS_LINK;     /* symlinks, and the exotic */
    }
}

static int fill_node(struct ext2fs *e, unsigned long ino, struct vfs_node *out)
{
    int rc = get_inode(e, ino);

    if (rc != EXT2_OK)
        return to_vfs(rc);
    out->type = type_of(e->inode.mode);
    out->size = e->inode.size;
    out->priv[0] = ino;
    return VFS_OK;
}

static void ext2fs_root(void *fsdata, struct vfs_node *out)
{
    if (fill_node(fsdata, EXT2_ROOT_INO, out) != VFS_OK) {
        out->type = VFS_DIR;            /* an unreadable root: lookups will fail */
        out->size = 0;
        out->priv[0] = EXT2_ROOT_INO;
    }
}

static int ext2fs_lookup(void *fsdata, const struct vfs_node *dir, const char *name,
                         struct vfs_node *out)
{
    struct ext2fs *e = fsdata;
    unsigned long ino;
    int rc = get_inode(e, dir->priv[0]);

    if (rc == EXT2_OK)
        rc = ext2_lookup(&e->fs, &e->inode, name, 1, &e->dirent, &ino);
    if (rc != EXT2_OK)
        return to_vfs(rc);
    return fill_node(e, ino, out);
}

static long ext2fs_read(void *fsdata, struct vfs_node *node, unsigned long offset,
                        void *buf, unsigned long len)
{
    struct ext2fs *e = fsdata;
    int rc = get_inode(e, node->priv[0]);
    long n;

    if (rc != EXT2_OK)
        return to_vfs(rc);
    n = ext2_read(&e->fs, &e->inode, offset, buf, len);
    return n < 0 ? to_vfs((int)n) : n;
}

static int ext2fs_readdir(void *fsdata, const struct vfs_node *dir,
                          unsigned long *cookie, struct vfs_dirent *out)
{
    struct ext2fs *e = fsdata;
    struct ext2_inode child;
    int rc = get_inode(e, dir->priv[0]), i;

    if (rc == EXT2_OK)
        rc = ext2_readdir(&e->fs, &e->inode, cookie, &e->dirent);
    if (rc == EXT2_NOT_FOUND)
        return 0;
    if (rc != EXT2_OK)
        return to_vfs(rc);

    for (i = 0; e->dirent.name[i]; i++)
        out->name[i] = e->dirent.name[i];
    out->name[i] = '\0';

    /* The size is in the inode, not the directory. A listing wants it. */
    rc = ext2_read_inode(&e->fs, e->dirent.ino, &child);
    if (rc != EXT2_OK)
        return to_vfs(rc);
    out->type = type_of(child.mode);
    out->size = child.size;
    return 1;
}


/* =================================================================== writing
 *
 * Crash safety is in the ORDER things reach the disk, and the block cache is
 * write-through so that the order here is the order there:
 *
 *   growing a file   bitmap, then the data, then the inode that points at it
 *   creating         bitmap, the inode, and only then the directory entry
 *   removing         the directory entry first, then the inode, then bitmaps
 *
 * Interrupt any of those anywhere and the worst on disk is a block or an
 * inode marked used that nothing refers to - a leak, which e2fsck reclaims.
 * Never a directory entry naming an inode that was not written, and never a
 * pointer to a block somebody else may be given. There is no journal; this
 * is what stands in for one.
 */

/* superblock */
#define SB_FREE_BLOCKS   12
#define SB_FREE_INODES   16
#define SB_WTIME         48
#define SB_STATE         58
#define SB_STATE_VALID   1

/* group descriptor */
#define GD_SIZE          32
#define GD_BLOCK_BITMAP  0
#define GD_INODE_BITMAP  4
#define GD_INODE_TABLE   8
#define GD_FREE_BLOCKS   12
#define GD_FREE_INODES   14
#define GD_USED_DIRS     16

/* raw inode */
#define I_MODE    0
#define I_SIZE    4
#define I_ATIME   8
#define I_CTIME   12
#define I_MTIME   16
#define I_DTIME   20
#define I_LINKS   26
#define I_BLOCKS  28            /* in 512-byte units, whatever the block size */
#define I_FLAGS   32
#define I_BLOCK   40
#define INDEX_FL  0x1000        /* an htree directory */

/* directory entry */
#define DE_INODE     0
#define DE_REC_LEN   4
#define DE_NAME_LEN  6
#define DE_FILE_TYPE 7
#define DE_NAME      8
#define FT_REG       1
#define FT_DIR       2

#define g16(p)    le16_get(p)
#define g32(p)    le32_get(p)
#define p16(p, v) le16_put((p), (v))
#define p32(p, v) le32_put((p), (v))

static int rd(struct ext2fs *e, unsigned long blk, unsigned char *buf)
{
    if (blk == 0 || blk >= e->fs.blocks_count)
        return VFS_EIO;
    return e->fs.dev->read(e->fs.dev, blk << e->fs.log_sectors,
                           (unsigned)e->fs.sectors_per_block, buf) ? VFS_EIO : VFS_OK;
}

static int wr(struct ext2fs *e, unsigned long blk, const unsigned char *buf)
{
    if (blk == 0 || blk >= e->fs.blocks_count)
        return VFS_EIO;
    return e->fs.dev->write(e->fs.dev, blk << e->fs.log_sectors,
                            (unsigned)e->fs.sectors_per_block, buf) ? VFS_EIO : VFS_OK;
}

/* Longwords where the alignment allows: these run once per block written,
 * and a byte loop is four times the instructions on a CPU with no cache to
 * hide it. Block buffers are always long aligned; callers' may not be. */
/* File data, whole blocks, even address: around the cache if there is a
 * way round. */
static int wr_run(struct ext2fs *e, unsigned long blk, unsigned long blocks,
                  const unsigned char *buf)
{
    const struct blkdev *dev = e->fs.dev;

    if (blk == 0 || blk + blocks > e->fs.blocks_count)
        return VFS_EIO;
    return (dev->bulk_write ? dev->bulk_write : dev->write)
               (dev, blk << e->fs.log_sectors,
                (unsigned)(blocks << e->fs.log_sectors), buf) ? VFS_EIO : VFS_OK;
}

static void zero(unsigned char *buf, unsigned long n)
{
    if (!((unsigned long)buf & 3))
        for (; n >= 4; n -= 4, buf += 4)
            *(unsigned long *)buf = 0;
    while (n--)
        *buf++ = 0;
}

static void copy(unsigned char *to, const unsigned char *from, unsigned long n)
{
    if (!(((unsigned long)to | (unsigned long)from) & 3))
        for (; n >= 4; n -= 4, to += 4, from += 4)
            *(unsigned long *)to = *(const unsigned long *)from;
    while (n--)
        *to++ = *from++;
}

/* ------------------------------------------------- the write-back buffers --- */

#define WB_BITMAP 0
#define WB_M1     1
#define WB_M2     2

static unsigned char *wb_buf(struct ext2fs *e, int which)
{
    return which == WB_BITMAP ? e->bbuf : which == WB_M1 ? e->m1 : e->m2;
}

static int wb_flush(struct ext2fs *e, int which)
{
    int rc = VFS_OK;

    if (e->dirty[which] && e->held[which])
        rc = wr(e, e->held[which], wb_buf(e, which));
    e->dirty[which] = 0;
    return rc;
}

/* Everything the inode about to be written depends on. */
static int wb_flush_all(struct ext2fs *e)
{
    int a = wb_flush(e, WB_BITMAP), b = wb_flush(e, WB_M1), c = wb_flush(e, WB_M2);

    return (a != VFS_OK || b != VFS_OK || c != VFS_OK) ? VFS_EIO : VFS_OK;
}

/* Block `blk` into buffer `which`, writing out what was there if it had
 * changed. `fresh` means the block is new: nothing to read, start from zeros. */
static int wb_load(struct ext2fs *e, int which, unsigned long blk, int fresh)
{
    if (e->held[which] == blk)
        return VFS_OK;
    if (wb_flush(e, which) != VFS_OK)
        return VFS_EIO;
    e->held[which] = 0;
    if (fresh)
        zero(wb_buf(e, which), e->fs.block_size);
    else if (rd(e, blk, wb_buf(e, which)) != VFS_OK)
        return VFS_EIO;
    e->held[which] = blk;
    e->dirty[which] = fresh;
    return VFS_OK;
}

static unsigned char *gd(struct ext2fs *e, unsigned long group)
{
    return e->gdt + group * GD_SIZE;
}

static int writer_init(struct ext2fs *e)
{
    unsigned long bs = e->fs.block_size, i;

    e->gdt_blocks = (e->fs.groups * GD_SIZE + bs - 1) / bs;
    e->gdt  = mem_alloc(e->gdt_blocks * bs, ALLOC_ANY);
    e->ibuf = mem_alloc(5 * bs, ALLOC_ANY);
    if (!e->gdt || !e->ibuf) {
        mem_free(e->gdt);
        mem_free(e->ibuf);
        return VFS_EIO;
    }
    e->bbuf = e->ibuf + bs;
    e->dbuf = e->bbuf + bs;
    e->m1   = e->dbuf + bs;
    e->m2   = e->m1 + bs;
    e->iblk = 0;
    e->rotor_map = e->rotor_bit = 0;
    e->meta_dirty = e->marked_dirty = 0;
    for (i = 0; i < 3; i++) {
        e->held[i] = 0;
        e->dirty[i] = 0;
    }

    if (e->fs.dev->read(e->fs.dev, 2, 2, e->sb) != 0)
        return VFS_EIO;
    for (i = 0; i < e->gdt_blocks; i++)
        if (rd(e, e->fs.first_data_block + 1 + i, e->gdt + i * bs) != VFS_OK)
            return VFS_EIO;
    e->now = g32(e->sb + SB_WTIME);

    if (!(g16(e->sb + SB_STATE) & SB_STATE_VALID))
        pr_warn("ext2: %s was not cleanly unmounted - run e2fsck on it\n",
                e->fs.dev->name);
    return VFS_OK;
}

/* Before the first change: say on disk that the volume is in use, so that
 * a crash from here on is visible as one. sync says it is clean again. */
static int begin_changes(struct ext2fs *e)
{
    if (!e->fs.dev->write)
        return VFS_EROFS;
    if (!e->marked_dirty) {
        p16(e->sb + SB_STATE, g16(e->sb + SB_STATE) & ~(unsigned long)SB_STATE_VALID);
        if (e->fs.dev->write(e->fs.dev, 2, 2, e->sb) != 0)
            return VFS_EIO;
        e->marked_dirty = 1;
    }
    e->meta_dirty = 1;
    return VFS_OK;
}

static void end_changes(struct ext2fs *e)
{
    /* The reader keeps map blocks and an inode it has parsed. Both may have
     * just changed under it. */
    ext2_forget(&e->fs);
    e->inode.ino = 0;
}

static int ext2fs_sync(void *fsdata)
{
    struct ext2fs *e = fsdata;
    unsigned long i;

    if (wb_flush_all(e) != VFS_OK)
        return VFS_EIO;
    if (!e->meta_dirty)
        return VFS_OK;
    for (i = 0; i < e->gdt_blocks; i++)
        if (wr(e, e->fs.first_data_block + 1 + i, e->gdt + i * e->fs.block_size) != VFS_OK)
            return VFS_EIO;
    p16(e->sb + SB_STATE, g16(e->sb + SB_STATE) | SB_STATE_VALID);
    if (e->fs.dev->write(e->fs.dev, 2, 2, e->sb) != 0)
        return VFS_EIO;
    e->meta_dirty = e->marked_dirty = 0;
    return VFS_OK;
}

/* ------------------------------------------------------------- bitmaps --- */

/* How many blocks group g really has: the last one is usually short. */
static unsigned long blocks_in_group(struct ext2fs *e, unsigned long g)
{
    unsigned long start = e->fs.first_data_block + g * e->fs.blocks_per_group;
    unsigned long left = e->fs.blocks_count - start;

    return left < e->fs.blocks_per_group ? left : e->fs.blocks_per_group;
}

/* The first clear bit in [from, to), set; or -1. */
static long take_bit_in(unsigned char *map, unsigned long from, unsigned long to)
{
    unsigned long i = from;

    while (i < to) {
        if (!(i & 7) && map[i >> 3] == 0xFF) {
            i += 8;                     /* a full byte: skip it whole */
            continue;
        }
        if (!(map[i >> 3] & (1u << (i & 7)))) {
            map[i >> 3] |= (unsigned char)(1u << (i & 7));
            return (long)i;
        }
        i++;
    }
    return -1;
}

/* A clear bit below `limit`, set: from `start` onwards for preference, then
 * from the beginning. -1 if there is none. */
static long take_bit(unsigned char *map, unsigned long limit, unsigned long start)
{
    long bit = -1;

    if (start < limit)
        bit = take_bit_in(map, start, limit);
    if (bit < 0)
        bit = take_bit_in(map, 0, start < limit ? start : limit);
    return bit;
}

/* A free block, from `goal`'s group if it has one. 0 if the disk is full. */
static unsigned long alloc_block(struct ext2fs *e, unsigned long goal)
{
    unsigned long n;

    unsigned long g = goal < e->fs.groups ? goal : 0;

    for (n = 0; n < e->fs.groups; n++, g = (g + 1 == e->fs.groups) ? 0 : g + 1) {
        unsigned char *d = gd(e, g);
        long bit;

        if (g16(d + GD_FREE_BLOCKS) == 0)
            continue;
        if (wb_load(e, WB_BITMAP, g32(d + GD_BLOCK_BITMAP), 0) != VFS_OK)
            return 0;
        bit = take_bit(e->bbuf, blocks_in_group(e, g),
                       e->rotor_map == g32(d + GD_BLOCK_BITMAP) ? e->rotor_bit : 0);
        if (bit < 0)
            continue;                   /* the count lied; e2fsck will say so */
        e->rotor_map = g32(d + GD_BLOCK_BITMAP);
        e->rotor_bit = (unsigned long)bit + 1;
        e->dirty[WB_BITMAP] = 1;
        p16(d + GD_FREE_BLOCKS, g16(d + GD_FREE_BLOCKS) - 1);
        p32(e->sb + SB_FREE_BLOCKS, g32(e->sb + SB_FREE_BLOCKS) - 1);
        return e->fs.first_data_block + g * e->fs.blocks_per_group + (unsigned long)bit;
    }
    return 0;
}

static void free_block(struct ext2fs *e, unsigned long blk)
{
    unsigned long rel, g, bit;
    unsigned char *d;

    if (blk < e->fs.first_data_block || blk >= e->fs.blocks_count)
        return;
    rel = blk - e->fs.first_data_block;
    g   = rel / e->fs.blocks_per_group;
    bit = rel % e->fs.blocks_per_group;
    d   = gd(e, g);

    if (wb_load(e, WB_BITMAP, g32(d + GD_BLOCK_BITMAP), 0) != VFS_OK)
        return;
    if (!(e->bbuf[bit >> 3] & (1u << (bit & 7))))
        return;                         /* already free: do not count it twice */
    e->bbuf[bit >> 3] &= (unsigned char)~(1u << (bit & 7));
    e->dirty[WB_BITMAP] = 1;
    p16(d + GD_FREE_BLOCKS, g16(d + GD_FREE_BLOCKS) + 1);
    p32(e->sb + SB_FREE_BLOCKS, g32(e->sb + SB_FREE_BLOCKS) + 1);
}

static unsigned long alloc_inode(struct ext2fs *e, unsigned long goal, int is_dir)
{
    unsigned long n;

    for (n = 0; n < e->fs.groups; n++) {
        unsigned long g = (goal + n) % e->fs.groups;
        unsigned char *d = gd(e, g);
        long bit;

        if (g16(d + GD_FREE_INODES) == 0)
            continue;
        if (wb_load(e, WB_BITMAP, g32(d + GD_INODE_BITMAP), 0) != VFS_OK)
            return 0;
        bit = take_bit(e->bbuf, e->fs.inodes_per_group, 0);
        if (bit < 0)
            continue;
        e->dirty[WB_BITMAP] = 1;
        p16(d + GD_FREE_INODES, g16(d + GD_FREE_INODES) - 1);
        if (is_dir)
            p16(d + GD_USED_DIRS, g16(d + GD_USED_DIRS) + 1);
        p32(e->sb + SB_FREE_INODES, g32(e->sb + SB_FREE_INODES) - 1);
        return g * e->fs.inodes_per_group + (unsigned long)bit + 1;
    }
    return 0;
}

static void free_inode(struct ext2fs *e, unsigned long ino, int was_dir)
{
    unsigned long g = (ino - 1) / e->fs.inodes_per_group;
    unsigned long bit = (ino - 1) % e->fs.inodes_per_group;
    unsigned char *d = gd(e, g);

    if (wb_load(e, WB_BITMAP, g32(d + GD_INODE_BITMAP), 0) != VFS_OK)
        return;
    if (!(e->bbuf[bit >> 3] & (1u << (bit & 7))))
        return;
    e->bbuf[bit >> 3] &= (unsigned char)~(1u << (bit & 7));
    e->dirty[WB_BITMAP] = 1;
    p16(d + GD_FREE_INODES, g16(d + GD_FREE_INODES) + 1);
    if (was_dir)
        p16(d + GD_USED_DIRS, g16(d + GD_USED_DIRS) - 1);
    p32(e->sb + SB_FREE_INODES, g32(e->sb + SB_FREE_INODES) + 1);
}

/* --------------------------------------------------------------- inodes --- */

/* The raw inode, in ibuf. Valid until the next iget; iput writes it back.
 * Only one inode is ever held at a time. */
static unsigned char *iget(struct ext2fs *e, unsigned long ino)
{
    unsigned long g, offset, blk;

    if (ino == 0 || ino > e->fs.inodes_count)
        return 0;
    g = (ino - 1) / e->fs.inodes_per_group;
    offset = ((ino - 1) % e->fs.inodes_per_group) * e->fs.inode_size;
    blk = g32(gd(e, g) + GD_INODE_TABLE) + offset / e->fs.block_size;

    if (e->iblk != blk) {
        e->iblk = 0;
        if (rd(e, blk, e->ibuf) != VFS_OK)
            return 0;
        e->iblk = blk;
    }
    return e->ibuf + offset % e->fs.block_size;
}

/* Bitmaps and map blocks first, always: an inode must never reach the disk
 * pointing at blocks the bitmap still calls free, or through a map block
 * that is not there yet. */
static int iput(struct ext2fs *e)
{
    if (wb_flush_all(e) != VFS_OK)
        return VFS_EIO;
    return e->iblk ? wr(e, e->iblk, e->ibuf) : VFS_EIO;
}

static unsigned long group_of(struct ext2fs *e, unsigned long ino)
{
    return (ino - 1) / e->fs.inodes_per_group;
}

/* ------------------------------------------------------------ block map --- */

/* One step down the map: the pointer at `slot`, allocating it if it is a
 * hole and `create` is set. A newly allocated MAP block is zeroed on disk
 * before anything points at it. *fresh says the block is new. */
static unsigned long map_step(struct ext2fs *e, unsigned char *raw, unsigned char *slot,
                              unsigned long goal, int create, int *fresh)
{
    unsigned long blk = g32(slot);

    *fresh = 0;
    if (blk || !create)
        return blk;
    blk = alloc_block(e, goal);
    if (!blk)
        return 0;
    p32(slot, blk);
    p32(raw + I_BLOCKS, g32(raw + I_BLOCKS) + e->fs.sectors_per_block);
    *fresh = 1;
    return blk;
}

/*
 * The disk block for file block n of the inode at `raw` (which is in ibuf
 * and is NOT written here - the caller does that last). 0 for a hole, or
 * for a full disk when creating.
 */
static unsigned long bmap_w(struct ext2fs *e, unsigned char *raw, unsigned long ino,
                            unsigned long n, int create, int *fresh)
{
    unsigned long per = e->fs.ptrs_per_block, goal = group_of(e, ino), blk, l1;
    int f;

    *fresh = 0;
    if (n < 12)
        return map_step(e, raw, raw + I_BLOCK + 4 * n, goal, create, fresh);
    n -= 12;

    if (n < per) {
        l1 = map_step(e, raw, raw + I_BLOCK + 4 * 12, goal, create, &f);
    } else {
        unsigned long l2;

        n -= per;
        if ((n >> e->fs.log_ptrs) >= per)
            return 0;                   /* triple indirection: not here */
        l2 = map_step(e, raw, raw + I_BLOCK + 4 * 13, goal, create, &f);
        if (!l2 || wb_load(e, WB_M2, l2, f) != VFS_OK)
            return 0;
        l1 = map_step(e, raw, e->m2 + 4 * (n >> e->fs.log_ptrs), goal, create, &f);
        if (f)
            e->dirty[WB_M2] = 1;
        n &= per - 1;
    }
    if (!l1 || wb_load(e, WB_M1, l1, f) != VFS_OK)
        return 0;
    blk = map_step(e, raw, e->m1 + 4 * n, goal, create, fresh);
    if (*fresh)
        e->dirty[WB_M1] = 1;
    return blk;
}

/* Free every block under a map block `levels` deep (0 = it is a data block).
 * Pointers are copied out a block at a time, since freeing below reuses the
 * buffers this would otherwise be reading from. */
static void free_tree(struct ext2fs *e, unsigned long blk, int levels)
{
    unsigned long i;

    if (!blk)
        return;
    if (levels == 1) {
        if (wb_load(e, WB_M1, blk, 0) == VFS_OK)
            for (i = 0; i < e->fs.ptrs_per_block; i++)
                free_block(e, g32(e->m1 + 4 * i));
    } else if (levels == 2) {
        for (i = 0; i < e->fs.ptrs_per_block; i++) {
            if (wb_load(e, WB_M2, blk, 0) != VFS_OK)
                break;
            free_tree(e, g32(e->m2 + 4 * i), 1);
        }
    }
    /* levels == 3 is never built here - bmap_w refuses - so there is nothing
     * under it that this driver allocated. */
    free_block(e, blk);
}

/* Empty the inode held in ibuf at `raw`. The inode is written FIRST, with
 * its pointers gone, and the blocks freed afterwards from a copy - so a
 * crash leaks blocks and never leaves a live pointer to a freed one. */
static int release_blocks(struct ext2fs *e, unsigned char *raw)
{
    unsigned long block[EXT2_NBLOCKS];
    int i;

    for (i = 0; i < EXT2_NBLOCKS; i++) {
        block[i] = g32(raw + I_BLOCK + 4 * i);
        p32(raw + I_BLOCK + 4 * i, 0);
    }
    p32(raw + I_SIZE, 0);
    p32(raw + I_BLOCKS, 0);
    p32(raw + I_MTIME, e->now);
    if (iput(e) != VFS_OK)
        return VFS_EIO;

    for (i = 0; i < 12; i++)
        free_tree(e, block[i], 0);
    free_tree(e, block[12], 1);
    free_tree(e, block[13], 2);
    free_tree(e, block[14], 3);
    return wb_flush_all(e);
}

/* ---------------------------------------------------------------- write --- */

static long ext2fs_write(void *fsdata, struct vfs_node *node, unsigned long offset,
                         const void *buf, unsigned long len)
{
    struct ext2fs *e = fsdata;
    const unsigned char *in = buf;
    unsigned long bs = e->fs.block_size, done = 0, size;
    unsigned char *raw;
    int rc = begin_changes(e);

    if (rc != VFS_OK)
        return rc;
    if (len == 0)
        return 0;
    if (offset + len < offset || offset + len > 0x7FFFFFFFUL)
        return VFS_EINVAL;              /* large_file is accepted, not written */
    raw = iget(e, node->priv[0]);
    if (!raw)
        return VFS_EIO;
    size = g32(raw + I_SIZE);

    while (done < len) {
        unsigned long n = (offset + done) >> e->fs.log_block, off = (offset + done) & (bs - 1);
        unsigned long run = bs - off, blk;
        int fresh;

        if (run > len - done)
            run = len - done;
        blk = bmap_w(e, raw, node->priv[0], n, 1, &fresh);
        if (!blk)
            break;                      /* the disk is full */

        if (off == 0 && len - done >= bs && !((unsigned long)(in + done) & 1)) {
            /*
             * Whole blocks from an even address go straight from the
             * caller's memory to the disk, and as many at once as the
             * allocator made adjacent - one command, no copies, nothing to
             * read first. The blocks are allocated before any of them is
             * written, and the inode still goes last.
             */
            unsigned long blocks = 1, want = (len - done) >> e->fs.log_block;
            int f;

            if (want > EXT2_MAX_RUN)
                want = EXT2_MAX_RUN;
            while (blocks < want &&
                   bmap_w(e, raw, node->priv[0], n + blocks, 1, &f) == blk + blocks)
                blocks++;
            /* A block that was allocated and turned out not to be adjacent
             * stays allocated and mapped: the next time round the loop picks
             * it up as the start of the next run. */
            if (wr_run(e, blk, blocks, in + done) != VFS_OK) {
                rc = VFS_EIO;
                break;
            }
            run = blocks << e->fs.log_block;
        } else {
            /* Otherwise what is there has to survive around the edges -
             * unless the block is new, and there is nothing there. */
            if (fresh || run == bs)
                zero(e->dbuf, bs);
            else if (rd(e, blk, e->dbuf) != VFS_OK) {
                rc = VFS_EIO;
                break;
            }
            copy(e->dbuf + off, in + done, run);
            if (wr(e, blk, e->dbuf) != VFS_OK) {
                rc = VFS_EIO;
                break;
            }
        }
        done += run;
    }

    /* The inode last: only now does the file claim the new blocks. */
    if (offset + done > size)
        size = offset + done;
    if (done) {
        p32(raw + I_SIZE, size);
        p32(raw + I_MTIME, e->now);
    }
    if (iput(e) != VFS_OK)
        rc = VFS_EIO;
    node->size = size;
    end_changes(e);

    if (done)
        return (long)done;
    return rc != VFS_OK ? rc : VFS_ENOSPC;
}

static int ext2fs_truncate(void *fsdata, struct vfs_node *node)
{
    struct ext2fs *e = fsdata;
    unsigned char *raw;
    int rc = begin_changes(e);

    if (rc != VFS_OK)
        return rc;
    raw = iget(e, node->priv[0]);
    rc = raw ? release_blocks(e, raw) : VFS_EIO;
    node->size = 0;
    end_changes(e);
    return rc;
}

/* ---------------------------------------------------------- directories --- */

static unsigned long entry_size(unsigned long name_len)
{
    return (DE_NAME + name_len + 3) & ~3UL;
}

static void fill_entry(struct ext2fs *e, unsigned char *at, unsigned long rec_len,
                       unsigned long ino, const char *name, unsigned long name_len,
                       unsigned type)
{
    unsigned long i;

    p32(at + DE_INODE, ino);
    p16(at + DE_REC_LEN, rec_len);
    at[DE_NAME_LEN] = (unsigned char)name_len;
    at[DE_FILE_TYPE] = (e->fs.feature_incompat & EXT2_INCOMPAT_FILETYPE)
                     ? (unsigned char)type : 0;
    for (i = 0; i < name_len; i++)
        at[DE_NAME + i] = (unsigned char)name[i];
}

/* Put (name -> ino) in directory `dir`: in slack after an existing entry, in
 * an unused one, or in a new block. */
static int add_entry(struct ext2fs *e, unsigned long dir, const char *name,
                     unsigned long ino, unsigned type)
{
    unsigned long bs = e->fs.block_size, name_len = str_len(name);
    unsigned long need = entry_size(name_len), pos, size, blk;
    unsigned char *raw = iget(e, dir);
    int fresh;

    if (!raw)
        return VFS_EIO;
    if (name_len == 0 || name_len > EXT2_NAME_MAX)
        return VFS_EINVAL;
    size = g32(raw + I_SIZE);

    /* A directory with a hash index that this does not maintain would be
     * searched wrongly by anything that trusts it. Dropping the flag turns
     * it back into the plain list it also is. */
    if (g32(raw + I_FLAGS) & INDEX_FL)
        p32(raw + I_FLAGS, g32(raw + I_FLAGS) & ~(unsigned long)INDEX_FL);

    for (pos = 0; pos < size; pos += bs) {
        unsigned long off = 0;

        blk = bmap_w(e, raw, dir, pos >> e->fs.log_block, 0, &fresh);
        if (!blk || rd(e, blk, e->dbuf) != VFS_OK)
            return VFS_EIO;
        while (off < bs) {
            unsigned char *at = e->dbuf + off;
            unsigned long rec = g16(at + DE_REC_LEN);
            unsigned long used = g32(at + DE_INODE) ? entry_size(at[DE_NAME_LEN]) : 0;

            if (rec < DE_NAME || (rec & 3) || off + rec > bs)
                return VFS_EIO;         /* corrupt: do not make it worse */
            if (rec - used >= need) {
                if (used) {             /* split: the old entry keeps what it uses */
                    p16(at + DE_REC_LEN, used);
                    at += used;
                    rec -= used;
                }
                fill_entry(e, at, rec, ino, name, name_len, type);
                if (wr(e, blk, e->dbuf) != VFS_OK)
                    return VFS_EIO;
                return iput(e);         /* for the flag, if it changed */
            }
            off += rec;
        }
    }

    /* No room anywhere: the directory grows by a block. */
    blk = bmap_w(e, raw, dir, size >> e->fs.log_block, 1, &fresh);
    if (!blk)
        return VFS_ENOSPC;
    zero(e->dbuf, bs);
    fill_entry(e, e->dbuf, bs, ino, name, name_len, type);
    if (wr(e, blk, e->dbuf) != VFS_OK)
        return VFS_EIO;
    p32(raw + I_SIZE, size + bs);
    p32(raw + I_MTIME, e->now);
    return iput(e);
}

/* Take the entry for inode `ino` out of directory `dir`. Its space goes to
 * the entry before it; the first in a block just becomes unused. */
static int remove_entry(struct ext2fs *e, unsigned long dir, unsigned long ino)
{
    unsigned long bs = e->fs.block_size, pos, size, blk;
    unsigned char *raw = iget(e, dir);
    int fresh;

    if (!raw)
        return VFS_EIO;
    size = g32(raw + I_SIZE);

    for (pos = 0; pos < size; pos += bs) {
        unsigned long off = 0, prev = bs;       /* bs: no previous entry */

        blk = bmap_w(e, raw, dir, pos >> e->fs.log_block, 0, &fresh);
        if (!blk || rd(e, blk, e->dbuf) != VFS_OK)
            return VFS_EIO;
        while (off < bs) {
            unsigned char *at = e->dbuf + off;
            unsigned long rec = g16(at + DE_REC_LEN);

            if (rec < DE_NAME || (rec & 3) || off + rec > bs)
                return VFS_EIO;
            if (g32(at + DE_INODE) == ino &&
                !(at[DE_NAME] == '.' && at[DE_NAME_LEN] <= 2)) {
                if (prev < bs)
                    p16(e->dbuf + prev + DE_REC_LEN,
                        g16(e->dbuf + prev + DE_REC_LEN) + rec);
                else
                    p32(at + DE_INODE, 0);
                return wr(e, blk, e->dbuf);
            }
            prev = off;
            off += rec;
        }
    }
    return VFS_ENOENT;
}

/* --------------------------------------------------- create and remove --- */

static int ext2fs_create(void *fsdata, const struct vfs_node *dir, const char *name,
                         unsigned long type, struct vfs_node *out)
{
    struct ext2fs *e = fsdata;
    unsigned long parent = dir->priv[0], ino, blk = 0, i;
    int is_dir = (type == VFS_DIR), rc = begin_changes(e);
    unsigned char *raw;

    if (rc != VFS_OK)
        return rc;

    /* 1. the bitmap says the inode is taken */
    ino = alloc_inode(e, group_of(e, parent), is_dir);
    if (!ino)
        return VFS_ENOSPC;

    /* 2. the inode itself - and for a directory, its first block */
    if (is_dir) {
        blk = alloc_block(e, group_of(e, ino));
        if (!blk) {
            free_inode(e, ino, 1);
            return VFS_ENOSPC;
        }
        zero(e->dbuf, e->fs.block_size);
        fill_entry(e, e->dbuf, 12, ino, ".", 1, FT_DIR);
        fill_entry(e, e->dbuf + 12, e->fs.block_size - 12, parent, "..", 2, FT_DIR);
        if (wr(e, blk, e->dbuf) != VFS_OK)
            return VFS_EIO;
    }
    raw = iget(e, ino);
    if (!raw)
        return VFS_EIO;
    for (i = 0; i < e->fs.inode_size; i++)
        raw[i] = 0;
    p16(raw + I_MODE, is_dir ? (EXT2_S_IFDIR | 0755) : (EXT2_S_IFREG | 0644));
    p16(raw + I_LINKS, is_dir ? 2 : 1);
    p32(raw + I_ATIME, e->now);
    p32(raw + I_CTIME, e->now);
    p32(raw + I_MTIME, e->now);
    if (is_dir) {
        p32(raw + I_SIZE, e->fs.block_size);
        p32(raw + I_BLOCKS, e->fs.sectors_per_block);
        p32(raw + I_BLOCK, blk);
    }
    if (iput(e) != VFS_OK)
        return VFS_EIO;

    /* 3. only now a name for it. Fail here and steps 1 and 2 are undone. */
    rc = add_entry(e, parent, name, ino, is_dir ? FT_DIR : FT_REG);
    if (rc != VFS_OK) {
        if (blk)
            free_block(e, blk);
        free_inode(e, ino, is_dir);
        wb_flush_all(e);
        end_changes(e);
        return rc;
    }
    if (is_dir) {                       /* the new ".." is a link to the parent */
        raw = iget(e, parent);
        if (raw) {
            p16(raw + I_LINKS, g16(raw + I_LINKS) + 1);
            iput(e);
        }
    }

    end_changes(e);
    out->type = type;
    out->size = is_dir ? e->fs.block_size : 0;
    out->priv[0] = ino;
    return VFS_OK;
}

static int ext2fs_remove(void *fsdata, const struct vfs_node *dir, const char *name)
{
    struct ext2fs *e = fsdata;
    unsigned long parent = dir->priv[0], ino, pos = 0;
    unsigned char *raw;
    int is_dir, rc;

    rc = get_inode(e, parent);
    if (rc == EXT2_OK)
        rc = ext2_lookup(&e->fs, &e->inode, name, 1, &e->dirent, &ino);
    if (rc != EXT2_OK)
        return to_vfs(rc);

    rc = get_inode(e, ino);
    if (rc != EXT2_OK)
        return to_vfs(rc);
    is_dir = (e->inode.mode & EXT2_S_IFMT) == EXT2_S_IFDIR;
    if (is_dir && ext2_readdir(&e->fs, &e->inode, &pos, &e->dirent) == EXT2_OK)
        return VFS_ENOTEMPTY;

    rc = begin_changes(e);
    if (rc != VFS_OK)
        return rc;

    /* 1. the name goes first: from here on nothing can find the inode */
    rc = remove_entry(e, parent, ino);
    if (rc != VFS_OK) {
        end_changes(e);
        return rc;
    }

    /* 2. the inode: no links, a deletion time, no blocks */
    raw = iget(e, ino);
    if (raw) {
        p16(raw + I_LINKS, 0);
        p32(raw + I_DTIME, e->now ? e->now : 1);
        release_blocks(e, raw);         /* writes the inode, then frees */
    }

    /* 3. and the bitmaps */
    free_inode(e, ino, is_dir);
    if (is_dir) {                       /* its ".." no longer links the parent */
        raw = iget(e, parent);
        if (raw) {
            p16(raw + I_LINKS, g16(raw + I_LINKS) - 1);
            iput(e);
        }
    }
    rc = wb_flush_all(e);
    end_changes(e);
    return rc;
}

const struct fs_ops ext2_fs = {
    "ext2", ext2fs_probe, ext2fs_mount, ext2fs_unmount,
    ext2fs_root, ext2fs_lookup, ext2fs_read, ext2fs_readdir,
    ext2fs_create, ext2fs_write, ext2fs_truncate, ext2fs_remove, ext2fs_sync
};
