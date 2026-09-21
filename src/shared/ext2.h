/*
 * ext2.h - reading ext2, shared by the ROM and the kernel
 *
 * Like fat16.c: no allocation, no printing, buffers from the caller, a block
 * device to read through. The ROM uses it to find and load the kernel; the
 * kernel's fs_ext2.c puts it behind the VFS and adds writing on top.
 *
 * What is understood: revision 0 and 1, block sizes 1K to 4K, any inode
 * size, the filetype, sparse_super and large_file features. Anything else
 * marked incompatible makes ext2_mount refuse - an extent tree or a journal
 * that needs replaying is not something to guess at.
 *
 * Everything on disk is little-endian and is picked apart by byte.
 */
#ifndef EXT2_H
#define EXT2_H

#include "blkdev.h"

#define EXT2_OK             0
#define EXT2_READ_ERROR   (-1)
#define EXT2_NOT_EXT2     (-2)
#define EXT2_UNSUPPORTED  (-3)      /* a feature this cannot honour */
#define EXT2_NEED_MEMORY  (-4)      /* work area too small: see ext2_mount */
#define EXT2_NOT_FOUND    (-5)
#define EXT2_CORRUPT      (-6)

#define EXT2_ROOT_INO       2
#define EXT2_NAME_MAX       255
#define EXT2_MAX_BLOCK      4096
#define EXT2_NBLOCKS        15      /* 12 direct, then 1x, 2x, 3x indirect */
#define EXT2_MAX_RUN        32      /* blocks in one transfer: 256 sectors, an
                                     * ATA command's limit, at 4K a block */

/* i_mode, the type bits */
#define EXT2_S_IFMT   0xF000
#define EXT2_S_IFREG  0x8000
#define EXT2_S_IFDIR  0x4000
#define EXT2_S_IFLNK  0xA000

/* features this code will accept */
#define EXT2_INCOMPAT_FILETYPE     0x0002
#define EXT2_ROCOMPAT_SPARSE_SUPER 0x0001
#define EXT2_ROCOMPAT_LARGE_FILE   0x0002

struct ext2 {
    const struct blkdev *dev;
    unsigned long block_size;
    unsigned long sectors_per_block;    /* block_size / 512 */
    unsigned long log_sectors;          /* shift: block number -> sector */
    unsigned long log_block;            /* shift: byte offset -> block. Blocks
                                         * and pointers-per-block are powers
                                         * of two, and a 68000 divides by
                                         * calling a routine: every / and %
                                         * on the data path is a shift or a
                                         * mask instead. */
    unsigned long log_ptrs;             /* shift: map index -> map block */
    unsigned long blocks_count;
    unsigned long inodes_count;
    unsigned long first_data_block;     /* 1 for 1K blocks, else 0 */
    unsigned long blocks_per_group;
    unsigned long inodes_per_group;
    unsigned long inode_size;
    unsigned long first_ino;
    unsigned long groups;
    unsigned long feature_incompat;
    unsigned long feature_ro_compat;
    unsigned long ptrs_per_block;       /* block_size / 4 */

    /* Three block-sized buffers, each remembering which block it holds:
     * [0] general - inode table, directory and file data
     * [1] the single-indirect block last used
     * [2] the double-indirect block last used
     * so that walking a large file does not re-read its map per block. */
    unsigned char *buf[3];
    unsigned long  held[3];
};

struct ext2_inode {
    unsigned long  ino;
    unsigned short mode;
    unsigned short links;
    unsigned long  size;
    unsigned long  flags;
    unsigned long  block[EXT2_NBLOCKS];
};

struct ext2_dirent {
    unsigned long ino;
    unsigned char type;                 /* from the entry, if FILETYPE; else 0 */
    char          name[EXT2_NAME_MAX + 1];
};

/*
 * `work` is scratch for the life of the mount: three blocks' worth. The
 * block size is not known until the superblock has been read, so: call with
 * at least 1024 bytes; if the answer is EXT2_NEED_MEMORY, fs->block_size
 * says how big a block is - come back with 3 * that.
 */
int ext2_mount(const struct blkdev *dev, struct ext2 *fs,
               void *work, unsigned long work_size);

int ext2_read_inode(struct ext2 *fs, unsigned long ino, struct ext2_inode *out);

/* The disk block holding file block `n`: 0 for a hole, < 0 for an error. */
long ext2_bmap(struct ext2 *fs, const struct ext2_inode *inode, unsigned long n);

/* Holes read as zeros. Returns bytes read, 0 at the end, < 0 on error. */
long ext2_read(struct ext2 *fs, const struct ext2_inode *inode,
               unsigned long offset, void *buf, unsigned long len);

/* *pos is a byte offset into the directory: 0 to start. EXT2_OK with *out
 * filled, EXT2_NOT_FOUND at the end. "." and ".." are skipped. */
int ext2_readdir(struct ext2 *fs, const struct ext2_inode *dir,
                 unsigned long *pos, struct ext2_dirent *out);

/* Exact name first; failing that, and if fold_case, the first entry that
 * matches ignoring ASCII case. `scratch` is a dirent to work in. */
int ext2_lookup(struct ext2 *fs, const struct ext2_inode *dir, const char *name,
                int fold_case, struct ext2_dirent *scratch, unsigned long *ino);

/* le.s: little-endian fields, loaded whole and byte-swapped. p must be even,
 * which every ext2 field is. */
unsigned long le16_get(const void *p);
unsigned long le32_get(const void *p);
void          le16_put(void *p, unsigned long v);
void          le32_put(void *p, unsigned long v);

/* For a writer sharing the buffers: block `blk` into buf[which], unless it
 * is already there. ext2_forget drops what a buffer claims to hold. */
int  ext2_load(struct ext2 *fs, int which, unsigned long blk);
void ext2_forget(struct ext2 *fs);

#endif /* EXT2_H */
