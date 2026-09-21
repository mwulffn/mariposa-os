/*
 * disk.c - the ROM's boot path across the disk
 *
 * The parsing lives in src/shared/rdb.c and src/shared/fat16.c and prints
 * nothing. This file is the policy: which blocks to look at, what to report,
 * and where the kernel goes. The kernel will one day walk the same
 * structures and want to say something completely different about them.
 *
 * The buffers are fixed addresses from hardware.i. They are boot-time only:
 * once SYSTEM.BIN is running this chip RAM belongs to the kernel.
 */
#include "blkdev.h"
#include "rdb.h"
#include "fat16.h"
#include "ext2.h"
#include "rom.h"

#define RDB_BUFFER      ((void *)0x020000UL)
#define PART_BUFFER     ((void *)0x021000UL)
#define FS_BOOT_BUFFER  ((void *)0x022000UL)
#define FS_FAT_BUFFER   ((void *)0x022200UL)
#define FS_DIR_BUFFER   ((void *)0x022400UL)
#define FS_VARS         ((struct fat16 *)0x023000UL)

#define KERNEL_LOAD_ADDR ((void *)0x200000UL)
#define KERNEL_MAX_BYTES 0x80000UL          /* 512KB */

const struct blkdev *blkdev_boot(void);

/* Geometry from the last successful find_rdb, needed by load_partition.
 * In chip RAM rather than a static, because this image has no writable data
 * section - see rom.ld. */
#define RDB_INFO ((struct rdb_info *)0x023100UL)

static void say(const char *s)
{
    rom_serial_put_string(s);
}

static void say1(const char *fmt, unsigned long a)
{
    unsigned long args[1];
    args[0] = a;
    rom_printf(fmt, args);
}

static void say2(const char *fmt, unsigned long a, unsigned long b)
{
    unsigned long args[2];
    args[0] = a;
    args[1] = b;
    rom_printf(fmt, args);
}

/* ---------------------------------------------------------------- RDB --- */

unsigned long rom_find_rdb(void)
{
    const struct blkdev *dev = blkdev_boot();
    struct rdb_info *info = RDB_INFO;
    int rc;

    say("RDB: Scanning blocks 0-15...\r\n");

    rc = rdb_find(dev, RDB_BUFFER, info);

    if (rc == RDB_READ_ERROR) {
        say1("RDB: Error reading block %d\r\n", info->found_block);
        return (unsigned long)-1;
    }
    if (rc != RDB_OK) {
        say("RDB: Not found in blocks 0-15\r\n");
        return (unsigned long)-1;
    }

    say1("RDB: Found at block %d\r\n",            info->found_block);
    say1("RDB: Block size: %d bytes\r\n",         info->block_bytes);
    say1("RDB: Cylinders: %d\r\n",                info->cylinders);
    say1("RDB: Heads: %d\r\n",                    info->heads);
    say1("RDB: Sectors: %d\r\n",                  info->sectors);
    say1("RDB: Partition list at block: %d\r\n",  info->partlist);
    return 0;
}

/*
 * Reads the partition block from LBA 1 rather than following RDB_PARTLIST,
 * and reports PART_NEXT without following it either - so only the first
 * partition is ever reachable. Both were true of the assembly and are listed
 * in CLAUDE.md's known issues; converting faithfully first keeps this change
 * reviewable, and the fix needs a test image whose partition list is not at
 * block 1 before it is worth making.
 */
unsigned long rom_load_partition(unsigned long *start_lba, unsigned long *sectors)
{
    const struct blkdev *dev = blkdev_boot();
    struct rdb_partition part;
    int rc;

    say("PART: Loading first partition...\r\n");

    rc = rdb_read_partition(dev, RDB_INFO, 1, PART_BUFFER, &part);

    if (rc == RDB_READ_ERROR) {
        say("PART: Error reading block\r\n");
        return (unsigned long)-1;
    }
    if (rc != RDB_OK) {
        say("PART: No valid partition found\r\n");
        return (unsigned long)-1;
    }

    say("PART: Partition found!\r\n");
    say1("PART: Name: %s\r\n", (unsigned long)part.name);
    say2("PART: Cylinders: %d to %d\r\n", part.low_cyl, part.high_cyl);
    say1("PART: Start LBA: %d\r\n", part.start_lba);
    say1("PART: Size: %d blocks\r\n", part.sectors);
    say1("PART: Filesystem starts at LBA %d\r\n", part.start_lba);
    say1("PART: Filesystem size: %d blocks\r\n", part.sectors);
    say1("PART: DosType: %x.l\r\n", part.dostype);
    say1("PART: Next partition at block: %d\r\n", part.next);

    *start_lba = part.start_lba;
    *sectors   = part.sectors;
    return 0;
}

/* -------------------------------------------------------------- FAT16 --- */

unsigned long rom_fat16_init(unsigned long partition_lba)
{
    const struct blkdev *dev = blkdev_boot();
    struct fat16 *fs = FS_VARS;
    int rc;

    say("FAT16: Initializing filesystem...\r\n");

    rc = fat16_mount(dev, partition_lba, FS_BOOT_BUFFER, fs);
    if (rc == FAT16_READ_ERROR) {
        say("FAT16: ERROR - Failed to read boot sector\r\n");
        return (unsigned long)-1;
    }
    if (rc != FAT16_OK) {
        say("FAT16: ERROR - Invalid boot signature\r\n");
        return (unsigned long)-1;
    }

    say2("FAT16: Bytes/sector: %x.l, Sec/cluster: %x.l\r\n",
         fs->bytes_per_sec, fs->sec_per_clus);
    {
        unsigned long args[3];
        args[0] = fs->reserved_sec;
        args[1] = fs->num_fats;
        args[2] = fs->fat_size;
        rom_printf("FAT16: Reserved: %x.l, FATs: %x.l, FAT size: %x.l\r\n", args);
    }
    say2("FAT16: Root entries: %x.l, Root start: %x.l\r\n",
         fs->root_ent_cnt, fs->root_dir_start);
    say1("FAT16: Data starts at sector %x.l\r\n", fs->data_start_sec);

    return 0;
}

unsigned long rom_fat16_find_file(const char *name11,
                                  unsigned long *cluster, unsigned long *size)
{
    const struct blkdev *dev = blkdev_boot();
    int rc;

    say("FAT16: Searching for SYSTEM.BIN...\r\n");

    rc = fat16_find(dev, FS_VARS, name11, FS_DIR_BUFFER, cluster, size);
    if (rc == FAT16_READ_ERROR) {
        say("FAT16: ERROR - Failed to read directory\r\n");
        return (unsigned long)-1;
    }
    if (rc != FAT16_OK) {
        say("FAT16: ERROR - File not found\r\n");
        return (unsigned long)-1;
    }

    say("FAT16: Found! ");
    say2("Cluster: %x.l, Size: %x.l bytes\r\n", *cluster, *size);
    return 0;
}

unsigned long rom_fat16_read_cluster(void *dst, unsigned long cluster)
{
    const struct blkdev *dev = blkdev_boot();

    if (fat16_read_cluster(dev, FS_VARS, cluster, dst) != FAT16_OK) {
        say("FAT16: ERROR - Failed to read cluster\r\n");
        return (unsigned long)-1;
    }
    return 0;
}

unsigned long rom_fat16_next_cluster(unsigned long cluster)
{
    const struct blkdev *dev = blkdev_boot();
    long next = fat16_next_cluster(dev, FS_VARS, cluster, FS_FAT_BUFFER);

    if (next < 0) {
        say("FAT16: ERROR - Failed to read FAT\r\n");
        return (unsigned long)-1;
    }
    return (unsigned long)next;
}

/* ---------------------------------------------------------- boot path --- */

/* ---------------------------------------------------------------- ext2 --- */

/* Scratch for ext2, in the boot-time buffer area above the FAT16 ones. The
 * ROM has no writable statics - it lives in ROM - so everything that is not
 * on the stack has an address. Three 4KB blocks is the most ext2.c asks. */
#define EXT2_VARS    ((struct ext2 *)0x023200UL)
#define EXT2_DIRENT  ((struct ext2_dirent *)0x023400UL)
#define EXT2_WORK    ((void *)0x024000UL)
#define EXT2_WORK_BYTES (3UL * EXT2_MAX_BLOCK)

/* ext2.c reads a device whose block 0 is the filesystem's. The ROM's device
 * is the whole disk, so it is handed a window onto it - built on the stack,
 * for the reason above. */
struct window {
    const struct blkdev *disk;
    unsigned long        start;
};

static int window_read(const struct blkdev *dev, unsigned long lba,
                       unsigned count, void *buf)
{
    const struct window *w = (const struct window *)dev->hw;

    return w->disk->read(w->disk, w->start + lba, count, buf);
}

/*
 * Load SYSTEM.BIN from an ext2 partition. Returns 0 and the size, -1 on
 * error, and 1 if there is no ext2 here at all - so that the caller can go
 * on to try FAT16 without an error having been printed about a filesystem
 * nobody said was there.
 */
static long ext2_load_system_bin(unsigned long partition_lba, unsigned long *file_size)
{
    struct ext2 *fs = EXT2_VARS;
    struct ext2_inode root, file;
    struct window win;
    struct blkdev dev;
    unsigned long ino;
    int rc;

    win.disk  = blkdev_boot();
    win.start = partition_lba;
    dev.name    = "boot";
    dev.read    = window_read;
    dev.present = 0;
    dev.hw      = &win;
    dev.write   = 0;
    dev.blocks  = 0;
    dev.bulk_read  = 0;
    dev.bulk_write = 0;

    rc = ext2_mount(&dev, fs, EXT2_WORK, EXT2_WORK_BYTES);
    if (rc == EXT2_NOT_EXT2)
        return 1;
    if (rc == EXT2_UNSUPPORTED) {
        say("EXT2: ERROR - this filesystem uses features the ROM cannot read\r\n");
        say("EXT2:         (extents or a journal? make it with mke2fs -t ext2)\r\n");
        return -1;
    }
    if (rc != EXT2_OK) {
        say("EXT2: ERROR - cannot read the superblock\r\n");
        return -1;
    }
    say2("EXT2: Block size %x.l, %x.l block groups\r\n", fs->block_size, fs->groups);

    /* SYSTEM.BIN, however its case was typed: ext2 will not fold it for us. */
    rc = ext2_read_inode(fs, EXT2_ROOT_INO, &root);
    if (rc == EXT2_OK)
        rc = ext2_lookup(fs, &root, "SYSTEM.BIN", 1, EXT2_DIRENT, &ino);
    if (rc == EXT2_NOT_FOUND) {
        say("EXT2: ERROR - SYSTEM.BIN not found in the root directory\r\n");
        return -1;
    }
    if (rc == EXT2_OK)
        rc = ext2_read_inode(fs, ino, &file);
    if (rc != EXT2_OK) {
        say("EXT2: ERROR - cannot read the directory\r\n");
        return -1;
    }
    if ((file.mode & EXT2_S_IFMT) != EXT2_S_IFREG) {
        say("EXT2: ERROR - SYSTEM.BIN is not a file\r\n");
        return -1;
    }
    say2("EXT2: Found SYSTEM.BIN, inode %x.l, %x.l bytes\r\n", ino, file.size);
    if (file.size > KERNEL_MAX_BYTES) {
        say("EXT2: ERROR - File too large (>512KB)\r\n");
        return -1;
    }

    if (ext2_read(fs, &file, 0, KERNEL_LOAD_ADDR, file.size) != (long)file.size) {
        say("EXT2: ERROR - read failed\r\n");
        return -1;
    }
    say1("EXT2: Loaded %x.l bytes\r\n", file.size);
    *file_size = file.size;
    return 0;
}

unsigned long rom_load_system_bin(unsigned long partition_lba,
                                  unsigned long *file_size)
{
    static const char name[FAT16_NAME_LEN + 1] = "SYSTEM  BIN";
    unsigned long cluster, size, remaining;
    unsigned char *dst = (unsigned char *)KERNEL_LOAD_ADDR;
    long ext2;

    /* ext2 first: its magic number is a far better test than FAT's, which
     * is two bytes that any PC-formatted anything ends with. */
    ext2 = ext2_load_system_bin(partition_lba, file_size);
    if (ext2 <= 0)
        return ext2 == 0 ? 0 : (unsigned long)-1;

    say("FAT16: LoadSystemBin called\r\n");

    if (rom_fat16_init(partition_lba) != 0)
        return (unsigned long)-1;

    if (rom_fat16_find_file(name, &cluster, &size) != 0)
        return (unsigned long)-1;

    if (size > KERNEL_MAX_BYTES) {
        say("FAT16: ERROR - File too large (>512KB)\r\n");
        return (unsigned long)-1;
    }

    say("FAT16: Loading file...\r\n");

    remaining = size;
    for (;;) {
        unsigned long chunk;
        unsigned long next;

        if (rom_fat16_read_cluster(dst, cluster) != 0)
            return (unsigned long)-1;

        chunk = fat16_cluster_bytes(FS_VARS);
        dst += chunk;
        if (remaining <= chunk)
            break;
        remaining -= chunk;

        next = rom_fat16_next_cluster(cluster);
        if (next == (unsigned long)-1)
            return (unsigned long)-1;
        if (next >= FAT16_EOF_MIN)
            break;
        cluster = next;
    }

    say1("FAT16: Loaded %x.l bytes\r\n", size);
    *file_size = size;
    return 0;
}
