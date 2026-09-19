/*
 * fat16.h - read-only FAT16
 *
 * Parsing and chain walking, nothing else. No output, no opinion about what
 * is being loaded or where it goes; callers pass in the block device, the
 * mounted state and their own scratch buffers.
 */
#ifndef FAT16_H
#define FAT16_H

#include "blkdev.h"

#define FAT16_OK            0
#define FAT16_READ_ERROR  (-1)
#define FAT16_BAD_BOOT    (-2)
#define FAT16_NOT_FOUND   (-3)

#define FAT16_SECTOR_SIZE   512
#define FAT16_NAME_LEN      11      /* 8.3, space padded, no dot */
#define FAT16_EOF_MIN       0xFFF8UL

/*
 * Mounted filesystem state.
 *
 * The layout is fixed: the ROM keeps this at FS_VARS ($23000) and the disk
 * tests assert field offsets against it, which is what pins the boot-sector
 * parsing. Do not reorder without updating tests/test_disk.c.
 *
 *   0  partition_lba    long
 *   4  bytes_per_sec    word
 *   6  sec_per_clus     byte
 *   8  reserved_sec     word
 *  10  num_fats         byte
 *  12  root_ent_cnt     word
 *  14  fat_size         word
 *  16  root_dir_start   word
 *  18  root_dir_secs    word
 *  20  data_start_sec   long
 *  24  cached_fat_sec   long
 */
struct fat16 {
    unsigned long  partition_lba;
    unsigned short bytes_per_sec;
    unsigned char  sec_per_clus;
    unsigned char  pad0;
    unsigned short reserved_sec;
    unsigned char  num_fats;
    unsigned char  pad1;
    unsigned short root_ent_cnt;
    unsigned short fat_size;
    unsigned short root_dir_start;
    unsigned short root_dir_secs;
    unsigned long  data_start_sec;
    unsigned long  cached_fat_sec;   /* -1 when the FAT buffer is stale */
};

/* Read and parse the partition's boot sector into `fs`. `boot_buf` is
 * scratch, at least one sector. */
int fat16_mount(const struct blkdev *dev, unsigned long partition_lba,
                void *boot_buf, struct fat16 *fs);

/* Scan the root directory for an 8.3 name, space padded, exactly
 * FAT16_NAME_LEN bytes and no dot - "SYSTEM  BIN". `dir_buf` is scratch. */
int fat16_find(const struct blkdev *dev, const struct fat16 *fs,
               const char *name11, void *dir_buf,
               unsigned long *cluster, unsigned long *size);

/* Read one whole cluster, sec_per_clus sectors, to `dst`. */
int fat16_read_cluster(const struct blkdev *dev, const struct fat16 *fs,
                       unsigned long cluster, void *dst);

/* Follow the chain. Returns the next cluster, or a negative error. A value
 * at or above FAT16_EOF_MIN is the end of the file, not an error.
 * `fat_buf` is a one-sector cache whose tag lives in fs->cached_fat_sec. */
long fat16_next_cluster(const struct blkdev *dev, struct fat16 *fs,
                        unsigned long cluster, void *fat_buf);

/* Bytes in a cluster. */
unsigned long fat16_cluster_bytes(const struct fat16 *fs);

#endif /* FAT16_H */
