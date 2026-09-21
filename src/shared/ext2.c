/*
 * ext2.c - reading ext2, shared by the ROM and the kernel
 *
 * See ext2.h.
 */
#include "ext2.h"

#define SB_OFFSET_SECTORS   2       /* the superblock is at byte 1024 */
#define SB_MAGIC            0xEF53

/* superblock fields, byte offsets */
#define SB_INODES_COUNT     0
#define SB_BLOCKS_COUNT     4
#define SB_FIRST_DATA_BLOCK 20
#define SB_LOG_BLOCK_SIZE   24
#define SB_BLOCKS_PER_GROUP 32
#define SB_INODES_PER_GROUP 40
#define SB_MAGIC_OFF        56
#define SB_REV_LEVEL        76
#define SB_FIRST_INO        84
#define SB_INODE_SIZE       88
#define SB_FEATURE_INCOMPAT 96
#define SB_FEATURE_ROCOMPAT 100

#define GD_SIZE             32
#define GD_INODE_TABLE      8

#define I_MODE              0
#define I_SIZE              4
#define I_LINKS             26
#define I_FLAGS             32
#define I_BLOCK             40

#define DE_INODE            0
#define DE_REC_LEN          4
#define DE_NAME_LEN         6
#define DE_FILE_TYPE        7
#define DE_NAME             8

#define NOTHING             0xFFFFFFFFUL

#define le16(p) le16_get(p)
#define le32(p) le32_get(p)

void ext2_forget(struct ext2 *fs)
{
    fs->held[0] = fs->held[1] = fs->held[2] = NOTHING;
}

int ext2_load(struct ext2 *fs, int which, unsigned long blk)
{
    if (fs->held[which] == blk)
        return EXT2_OK;
    if (blk >= fs->blocks_count)
        return EXT2_CORRUPT;            /* a pointer off the end of the disk */
    fs->held[which] = NOTHING;
    if (fs->dev->read(fs->dev, blk << fs->log_sectors,
                      (unsigned)fs->sectors_per_block, fs->buf[which]) != 0)
        return EXT2_READ_ERROR;
    fs->held[which] = blk;
    return EXT2_OK;
}

int ext2_mount(const struct blkdev *dev, struct ext2 *fs,
               void *work, unsigned long work_size)
{
    unsigned char *sb = work;
    unsigned long log_bs, rev;

    if (work_size < 1024)
        return EXT2_NEED_MEMORY;
    if (dev->read(dev, SB_OFFSET_SECTORS, 2, sb) != 0)
        return EXT2_READ_ERROR;
    if (le16(sb + SB_MAGIC_OFF) != SB_MAGIC)
        return EXT2_NOT_EXT2;

    log_bs = le32(sb + SB_LOG_BLOCK_SIZE);
    if (log_bs > 2)
        return EXT2_UNSUPPORTED;        /* blocks bigger than 4K */

    fs->dev               = dev;
    fs->block_size        = 1024UL << log_bs;
    fs->sectors_per_block = 2UL << log_bs;
    fs->log_sectors       = 1 + log_bs;
    fs->log_block         = 10 + log_bs;
    fs->log_ptrs          = 8 + log_bs;
    fs->ptrs_per_block    = fs->block_size >> 2;
    fs->blocks_count      = le32(sb + SB_BLOCKS_COUNT);
    fs->inodes_count      = le32(sb + SB_INODES_COUNT);
    fs->first_data_block  = le32(sb + SB_FIRST_DATA_BLOCK);
    fs->blocks_per_group  = le32(sb + SB_BLOCKS_PER_GROUP);
    fs->inodes_per_group  = le32(sb + SB_INODES_PER_GROUP);

    rev = le32(sb + SB_REV_LEVEL);
    if (rev == 0) {                     /* the original: fixed everything */
        fs->inode_size = 128;
        fs->first_ino  = 11;
        fs->feature_incompat = fs->feature_ro_compat = 0;
    } else {
        fs->inode_size = le16(sb + SB_INODE_SIZE);
        fs->first_ino  = le32(sb + SB_FIRST_INO);
        fs->feature_incompat  = le32(sb + SB_FEATURE_INCOMPAT);
        fs->feature_ro_compat = le32(sb + SB_FEATURE_ROCOMPAT);
    }

    /* "Incompatible" means exactly that: a reader that does not know the
     * feature must not touch the filesystem. Extents, a journal wanting
     * recovery, 64-bit block numbers - none of them is something to guess. */
    if (fs->feature_incompat & ~(unsigned long)EXT2_INCOMPAT_FILETYPE)
        return EXT2_UNSUPPORTED;
    if (!fs->blocks_per_group || !fs->inodes_per_group ||
        fs->inode_size < 128 || fs->inode_size > fs->block_size)
        return EXT2_CORRUPT;

    fs->groups = (fs->blocks_count - fs->first_data_block +
                  fs->blocks_per_group - 1) / fs->blocks_per_group;

    if (work_size < 3 * fs->block_size)
        return EXT2_NEED_MEMORY;        /* fs->block_size says how much */
    fs->buf[0] = (unsigned char *)work;
    fs->buf[1] = fs->buf[0] + fs->block_size;
    fs->buf[2] = fs->buf[1] + fs->block_size;
    ext2_forget(fs);
    return EXT2_OK;
}

int ext2_read_inode(struct ext2 *fs, unsigned long ino, struct ext2_inode *out)
{
    unsigned long group, index, table, offset, per_block;
    const unsigned char *raw;
    int i, rc;

    if (ino == 0 || ino > fs->inodes_count)
        return EXT2_CORRUPT;

    group = (ino - 1) / fs->inodes_per_group;
    index = (ino - 1) % fs->inodes_per_group;

    /* The group descriptors follow the superblock's block. */
    per_block = fs->block_size / GD_SIZE;
    rc = ext2_load(fs, 0, fs->first_data_block + 1 + group / per_block);
    if (rc != EXT2_OK)
        return rc;
    table = le32(fs->buf[0] + (group % per_block) * GD_SIZE + GD_INODE_TABLE);

    offset = index * fs->inode_size;
    rc = ext2_load(fs, 0, table + offset / fs->block_size);
    if (rc != EXT2_OK)
        return rc;
    raw = fs->buf[0] + offset % fs->block_size;

    out->ino   = ino;
    out->mode  = (unsigned short)le16(raw + I_MODE);
    out->links = (unsigned short)le16(raw + I_LINKS);
    out->size  = le32(raw + I_SIZE);
    out->flags = le32(raw + I_FLAGS);
    for (i = 0; i < EXT2_NBLOCKS; i++)
        out->block[i] = le32(raw + I_BLOCK + 4 * i);
    return EXT2_OK;
}

/* Entry `index` of the pointer block `blk`, read through buffer `which`. */
static long pointer(struct ext2 *fs, int which, unsigned long blk, unsigned long index)
{
    int rc;

    if (blk == 0)
        return 0;                       /* a hole in the map is a hole below it */
    rc = ext2_load(fs, which, blk);
    if (rc != EXT2_OK)
        return rc;
    return (long)le32(fs->buf[which] + 4 * index);
}

long ext2_bmap(struct ext2 *fs, const struct ext2_inode *inode, unsigned long n)
{
    unsigned long per = fs->ptrs_per_block;
    long blk;

    if (n < 12)
        return (long)inode->block[n];
    n -= 12;

    if (n < per)
        return pointer(fs, 1, inode->block[12], n);
    n -= per;

    if ((n >> fs->log_ptrs) < per) {
        blk = pointer(fs, 2, inode->block[13], n >> fs->log_ptrs);
        if (blk <= 0)
            return blk;
        return pointer(fs, 1, (unsigned long)blk, n & (per - 1));
    }

    /* Triple indirection starts at 64MB with 1K blocks and 4GB with 4K. It
     * would need a third map buffer; say so rather than read rubbish. */
    return EXT2_UNSUPPORTED;
}

long ext2_read(struct ext2 *fs, const struct ext2_inode *inode,
               unsigned long offset, void *buf, unsigned long len)
{
    unsigned char *out = buf;
    unsigned long done = 0;

    if (offset >= inode->size)
        return 0;
    if (len > inode->size - offset)
        len = inode->size - offset;

    while (done < len) {
        unsigned long n   = (offset + done) >> fs->log_block;
        unsigned long off = (offset + done) & (fs->block_size - 1);
        unsigned long run = fs->block_size - off, i;
        long blk = ext2_bmap(fs, inode, n);

        if (blk < 0)
            return blk;
        if (run > len - done)
            run = len - done;

        if (blk == 0) {
            for (i = 0; i < run; i++)   /* a hole reads as zeros */
                out[done + i] = 0;
        } else if (off == 0 && len - done >= fs->block_size &&
                   !((unsigned long)(out + done) & 1)) {
            /*
             * Whole blocks to an even address go straight from the disk into
             * the caller's memory - and as many at once as are adjacent on
             * the disk, in one command. On CompactFlash there is no seek, so
             * the command is the unit of overhead, and mke2fs and this
             * driver's allocator both lay files out in long runs. This is
             * most of any large read; the ROM loading the kernel is nothing
             * else.
             */
            unsigned long blocks = 1, want = (len - done) >> fs->log_block;
            int (*rd)(const struct blkdev *, unsigned long, unsigned, void *) =
                fs->dev->bulk_read ? fs->dev->bulk_read : fs->dev->read;

            if (want > EXT2_MAX_RUN)
                want = EXT2_MAX_RUN;
            while (blocks < want && ext2_bmap(fs, inode, n + blocks) == blk + (long)blocks)
                blocks++;
            if ((unsigned long)blk + blocks > fs->blocks_count)
                return EXT2_CORRUPT;
            if (rd(fs->dev, (unsigned long)blk << fs->log_sectors,
                   (unsigned)(blocks << (fs->log_sectors)), out + done) != 0)
                return EXT2_READ_ERROR;
            run = blocks << fs->log_block;
        } else {
            int rc = ext2_load(fs, 0, (unsigned long)blk);

            if (rc != EXT2_OK)
                return rc;
            for (i = 0; i < run; i++)
                out[done + i] = fs->buf[0][off + i];
        }
        done += run;
    }
    return (long)done;
}

int ext2_readdir(struct ext2 *fs, const struct ext2_inode *dir,
                 unsigned long *pos, struct ext2_dirent *out)
{
    while (*pos < dir->size) {
        unsigned long off = *pos & (fs->block_size - 1), rec_len, name_len, i;
        const unsigned char *e;
        long blk = ext2_bmap(fs, dir, *pos >> fs->log_block);
        int rc;

        if (blk <= 0)
            return blk < 0 ? (int)blk : EXT2_CORRUPT;   /* no holes in directories */
        rc = ext2_load(fs, 0, (unsigned long)blk);
        if (rc != EXT2_OK)
            return rc;

        e = fs->buf[0] + off;
        rec_len  = le16(e + DE_REC_LEN);
        name_len = e[DE_NAME_LEN];

        /* rec_len is how the list is walked, so a bad one must not be
         * followed: too short to hold its own name, not a multiple of 4, or
         * running off the end of the block. */
        if (rec_len < DE_NAME + name_len || (rec_len & 3) ||
            off + rec_len > fs->block_size)
            return EXT2_CORRUPT;
        *pos += rec_len;

        if (le32(e + DE_INODE) == 0)
            continue;                   /* an unused slot */
        if (e[DE_NAME] == '.' &&
            (name_len == 1 || (name_len == 2 && e[DE_NAME + 1] == '.')))
            continue;

        out->ino  = le32(e + DE_INODE);
        out->type = (fs->feature_incompat & EXT2_INCOMPAT_FILETYPE) ? e[DE_FILE_TYPE] : 0;
        for (i = 0; i < name_len; i++)
            out->name[i] = (char)e[DE_NAME + i];
        out->name[name_len] = '\0';
        return EXT2_OK;
    }
    return EXT2_NOT_FOUND;
}

static int same_name(const char *a, const char *b, int fold)
{
    for (;; a++, b++) {
        unsigned char x = (unsigned char)*a, y = (unsigned char)*b;

        if (fold) {
            if (x >= 'A' && x <= 'Z') x = (unsigned char)(x + 32);
            if (y >= 'A' && y <= 'Z') y = (unsigned char)(y + 32);
        }
        if (x != y)
            return 0;
        if (!x)
            return 1;
    }
}

int ext2_lookup(struct ext2 *fs, const struct ext2_inode *dir, const char *name,
                int fold_case, struct ext2_dirent *scratch, unsigned long *ino)
{
    unsigned long pos = 0, folded = 0;
    int rc;

    /* ext2 is case-sensitive, so a directory may hold Readme and README. An
     * exact match wins; failing that, the first that matches ignoring case. */
    while ((rc = ext2_readdir(fs, dir, &pos, scratch)) == EXT2_OK) {
        if (same_name(scratch->name, name, 0)) {
            *ino = scratch->ino;
            return EXT2_OK;
        }
        if (fold_case && !folded && same_name(scratch->name, name, 1))
            folded = scratch->ino;
    }
    if (rc != EXT2_NOT_FOUND)
        return rc;
    if (folded) {
        *ino = folded;
        return EXT2_OK;
    }
    return EXT2_NOT_FOUND;
}
