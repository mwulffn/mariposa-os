/*
 * fat16.c - read-only FAT16
 *
 * Transcribed from the assembly filesystem.s.
 */
#include "fat16.h"

/* BPB offsets in the boot sector */
#define BPB_BYTES_PER_SEC   11
#define BPB_SEC_PER_CLUS    13
#define BPB_RSVD_SEC_CNT    14
#define BPB_NUM_FATS        16
#define BPB_ROOT_ENT_CNT    17
#define BPB_FAT_SIZE_16     22
#define BPB_SIGNATURE       510

/* Directory entry offsets */
#define DIR_NAME            0
#define DIR_ATTR            11
#define DIR_FST_CLUSTER     26
#define DIR_FILE_SIZE       28
#define DIR_ENTRY_SIZE      32

#define ATTR_VOLUME_ID      0x08
#define ATTR_LONG_NAME      0x0F

#define DIR_ENTRY_FREE      0xE5
#define DIR_ENTRY_END       0x00

/* FAT structures are little-endian; the 68000 is not, so these are the one
 * place byte order is handled. Fields are not guaranteed aligned either. */
static unsigned long le16(const void *buf, unsigned off)
{
    const unsigned char *p = (const unsigned char *)buf + off;
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8);
}

static unsigned long le32(const void *buf, unsigned off)
{
    const unsigned char *p = (const unsigned char *)buf + off;
    return (unsigned long)p[0]        | ((unsigned long)p[1] << 8) |
           ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

unsigned long fat16_cluster_bytes(const struct fat16 *fs)
{
    return (unsigned long)fs->sec_per_clus * FAT16_SECTOR_SIZE;
}

int fat16_mount(const struct blkdev *dev, unsigned long partition_lba,
                void *boot_buf, struct fat16 *fs)
{
    const unsigned char *b = (const unsigned char *)boot_buf;

    fs->partition_lba  = partition_lba;
    fs->cached_fat_sec = 0xFFFFFFFFUL;

    if (dev->read(dev, partition_lba, 1, boot_buf) != 0)
        return FAT16_READ_ERROR;

    if (b[BPB_SIGNATURE] != 0x55 || b[BPB_SIGNATURE + 1] != 0xAA)
        return FAT16_BAD_BOOT;

    fs->bytes_per_sec = (unsigned short)le16(b, BPB_BYTES_PER_SEC);
    fs->sec_per_clus  = b[BPB_SEC_PER_CLUS];
    fs->reserved_sec  = (unsigned short)le16(b, BPB_RSVD_SEC_CNT);
    fs->num_fats      = b[BPB_NUM_FATS];
    fs->root_ent_cnt  = (unsigned short)le16(b, BPB_ROOT_ENT_CNT);
    fs->fat_size      = (unsigned short)le16(b, BPB_FAT_SIZE_16);

    fs->root_dir_start = (unsigned short)(fs->reserved_sec +
                             (unsigned long)fs->num_fats * fs->fat_size);
    /* Round up: a partial last sector of directory entries still counts. */
    fs->root_dir_secs  = (unsigned short)(((unsigned long)fs->root_ent_cnt *
                             DIR_ENTRY_SIZE + FAT16_SECTOR_SIZE - 1) /
                             FAT16_SECTOR_SIZE);
    fs->data_start_sec = (unsigned long)fs->root_dir_start + fs->root_dir_secs;

    return FAT16_OK;
}

static int name_matches(const unsigned char *entry, const char *name11)
{
    int i;

    for (i = 0; i < FAT16_NAME_LEN; i++) {
        if (entry[DIR_NAME + i] != (unsigned char)name11[i])
            return 0;
    }
    return 1;
}

int fat16_find(const struct blkdev *dev, const struct fat16 *fs,
               const char *name11, void *dir_buf,
               unsigned long *cluster, unsigned long *size)
{
    unsigned long sector;

    for (sector = 0; sector < fs->root_dir_secs; sector++) {
        unsigned char *buf = (unsigned char *)dir_buf;
        unsigned long lba  = fs->partition_lba + fs->root_dir_start + sector;
        int i;

        if (dev->read(dev, lba, 1, dir_buf) != 0)
            return FAT16_READ_ERROR;

        for (i = 0; i < FAT16_SECTOR_SIZE / DIR_ENTRY_SIZE; i++) {
            unsigned char *e = buf + (unsigned)i * DIR_ENTRY_SIZE;
            unsigned char attr;

            if (e[DIR_NAME] == DIR_ENTRY_END)
                return FAT16_NOT_FOUND;        /* end of directory */
            if (e[DIR_NAME] == DIR_ENTRY_FREE)
                continue;

            attr = e[DIR_ATTR];
            if (attr & ATTR_VOLUME_ID)
                continue;
            if ((attr & ATTR_LONG_NAME) == ATTR_LONG_NAME)
                continue;

            if (!name_matches(e, name11))
                continue;

            *cluster = le16(e, DIR_FST_CLUSTER);
            *size    = le32(e, DIR_FILE_SIZE);
            return FAT16_OK;
        }
    }

    return FAT16_NOT_FOUND;
}

int fat16_read_cluster(const struct blkdev *dev, const struct fat16 *fs,
                       unsigned long cluster, void *dst)
{
    /* Clusters are numbered from 2: the first two FAT entries are reserved. */
    unsigned long lba = fs->partition_lba + fs->data_start_sec +
                        (cluster - 2) * fs->sec_per_clus;

    if (dev->read(dev, lba, fs->sec_per_clus, dst) != 0)
        return FAT16_READ_ERROR;
    return FAT16_OK;
}

long fat16_next_cluster(const struct blkdev *dev, struct fat16 *fs,
                        unsigned long cluster, void *fat_buf)
{
    unsigned long byte_off = cluster * 2;          /* 16-bit FAT entries */
    unsigned long sector   = byte_off / FAT16_SECTOR_SIZE;
    unsigned long in_sec   = byte_off % FAT16_SECTOR_SIZE;
    unsigned long lba      = fs->partition_lba + fs->reserved_sec + sector;

    if (lba != fs->cached_fat_sec) {
        fs->cached_fat_sec = lba;
        if (dev->read(dev, lba, 1, fat_buf) != 0)
            return FAT16_READ_ERROR;
    }

    return (long)le16(fat_buf, (unsigned)in_sec);
}
