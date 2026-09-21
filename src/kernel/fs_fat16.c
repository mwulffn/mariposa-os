/*
 * fs_fat16.c - FAT16 behind the VFS
 *
 * Read-only, 8.3 names, subdirectories. The parsing is src/shared/fat16.c,
 * the same code the ROM boots with; this file is the adapter. It exists for
 * the boot partition and for carrying files between machines, not as
 * somewhere to live.
 */
#include "vfs.h"
#include "fat16.h"
#include "mem.h"
#include "kstring.h"

struct fatfs {
    const struct blkdev *dev;
    struct fat16  fs;
    unsigned char sector[FAT16_SECTOR_SIZE];    /* directory and file data */
    unsigned char fat[FAT16_SECTOR_SIZE];       /* fat16.c's one-sector FAT cache */
};

/* node.priv: [0] first cluster (0 = the root directory)
 *            [1] cluster the last read ended in   } so that reading a file
 *            [2] its index in the chain           } from start to end is not
 *                                                   quadratic in its length */
#define N_FIRST  0
#define N_CUR    1
#define N_INDEX  2

static int fat_probe(const struct blkdev *dev)
{
    static unsigned char boot[FAT16_SECTOR_SIZE];
    struct fat16 fs;

    return fat16_mount(dev, 0, boot, &fs) == FAT16_OK;
}

static int fat_mount(const struct blkdev *dev, void **fsdata)
{
    struct fatfs *f = mem_alloc(sizeof *f, ALLOC_ANY);

    if (!f)
        return VFS_EIO;
    f->dev = dev;
    /* The device IS the partition, so the volume starts at its block 0. */
    if (fat16_mount(dev, 0, f->sector, &f->fs) != FAT16_OK) {
        mem_free(f);
        return VFS_ENODEV;
    }
    *fsdata = f;
    return VFS_OK;
}

static void fat_unmount(void *fsdata)
{
    mem_free(fsdata);
}

static void fat_root(void *fsdata, struct vfs_node *out)
{
    (void)fsdata;
    out->type = VFS_DIR;
    out->size = 0;
    out->priv[N_FIRST] = out->priv[N_CUR] = out->priv[N_INDEX] = 0;
}

static void to_node(const struct fat16_dirent *e, struct vfs_node *out)
{
    out->type = (e->attr & FAT16_ATTR_DIR) ? VFS_DIR : VFS_FILE;
    out->size = e->size;
    out->priv[N_FIRST] = out->priv[N_CUR] = e->cluster;
    out->priv[N_INDEX] = 0;
}

static int fat_readdir(void *fsdata, const struct vfs_node *dir,
                       unsigned long *cookie, struct vfs_dirent *out)
{
    struct fatfs *f = fsdata;
    struct fat16_dirent e;
    int rc, i;

    rc = fat16_readdir(f->dev, &f->fs, dir->priv[N_FIRST], cookie,
                       f->sector, f->fat, &e);
    if (rc == FAT16_NOT_FOUND)
        return 0;
    if (rc != FAT16_OK)
        return VFS_EIO;

    for (i = 0; e.name[i]; i++)
        out->name[i] = e.name[i];
    out->name[i] = '\0';
    out->type = (e.attr & FAT16_ATTR_DIR) ? VFS_DIR : VFS_FILE;
    out->size = e.size;
    return 1;
}

static int fat_lookup(void *fsdata, const struct vfs_node *dir, const char *name,
                      struct vfs_node *out)
{
    struct fatfs *f = fsdata;
    unsigned long first = dir->priv[N_FIRST], pos = 0;
    struct fat16_dirent e;
    int rc;

    while ((rc = fat16_readdir(f->dev, &f->fs, first, &pos,
                               f->sector, f->fat, &e)) == FAT16_OK)
        if (str_caseeq(e.name, name)) {
            to_node(&e, out);
            return VFS_OK;
        }
    return rc == FAT16_NOT_FOUND ? VFS_ENOENT : VFS_EIO;
}

static long fat_read(void *fsdata, struct vfs_node *node, unsigned long offset,
                     void *buf, unsigned long len)
{
    struct fatfs *f = fsdata;
    unsigned long csize = fat16_cluster_bytes(&f->fs);
    unsigned char *out = buf;
    unsigned long want, index, done = 0;

    if (offset >= node->size)
        return 0;
    if (len > node->size - offset)
        len = node->size - offset;

    /* Which cluster of the chain holds `offset`? Found by subtracting, not
     * dividing: a 68000 divides 32 bits by calling a routine. */
    for (index = 0, want = offset; want >= csize; want -= csize)
        index++;

    if (index < node->priv[N_INDEX]) {          /* going backwards: start over */
        node->priv[N_CUR] = node->priv[N_FIRST];
        node->priv[N_INDEX] = 0;
    }

    while (done < len) {
        unsigned long in_cluster, n, i;

        while (node->priv[N_INDEX] < index) {
            long next = fat16_next_cluster(f->dev, &f->fs, node->priv[N_CUR], f->fat);

            if (next < 2 || (unsigned long)next >= FAT16_EOF_MIN)
                return VFS_EIO;                 /* the chain is shorter than the size */
            node->priv[N_CUR] = (unsigned long)next;
            node->priv[N_INDEX]++;
        }

        /* Sector by sector through the cluster: no buffer a cluster big. */
        in_cluster = want;
        {
            unsigned long sector = in_cluster >> 9, off = in_cluster & 511;
            unsigned long lba = f->fs.partition_lba + f->fs.data_start_sec +
                                (node->priv[N_CUR] - 2) * f->fs.sec_per_clus;

            if (f->dev->read(f->dev, lba + sector, 1, f->sector) != 0)
                return VFS_EIO;

            n = FAT16_SECTOR_SIZE - off;
            if (n > len - done)
                n = len - done;
            for (i = 0; i < n; i++)
                out[done + i] = f->sector[off + i];
        }
        done += n;
        want += n;
        if (want >= csize) {
            want -= csize;
            index++;
        }
    }
    return (long)done;
}

const struct fs_ops fat16_fs = {
    "fat16", fat_probe, fat_mount, fat_unmount,
    fat_root, fat_lookup, fat_read, fat_readdir,
    0, 0, 0, 0, 0                       /* read-only: nothing that writes */
};
