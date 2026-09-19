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

unsigned long rom_load_system_bin(unsigned long partition_lba,
                                  unsigned long *file_size)
{
    static const char name[FAT16_NAME_LEN + 1] = "SYSTEM  BIN";
    unsigned long cluster, size, remaining;
    unsigned char *dst = (unsigned char *)KERNEL_LOAD_ADDR;

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
