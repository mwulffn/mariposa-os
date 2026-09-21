/*
 * test_disk.c - src/rom/ide.s, partition.s and filesystem.s
 *
 * The whole boot-time storage path, headless: an ATA register model over a
 * generated raw image (tests/mkdisk.py) carrying an Amiga RDB, one partition
 * and a FAT16 filesystem with SYSTEM.BIN on it. No boot.hdf, no mtools, no
 * emulator.
 *
 * SYSTEM.BIN's cluster chain is deliberately scattered and its contents are
 * position dependent, so a broken chain walk cannot pass by accident.
 */
#include "protocol.h"
#include "disk_layout.h"

#include <stdio.h>
#include <string.h>

static const char *g_disk_dir = "build";

void t_set_disk_dir(const char *dir) { g_disk_dir = dir; }

static int attach(const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", g_disk_dir, name);
    return h_attach_disk(path);
}

/* Somewhere in chip RAM clear of the ROM's own buffers ($20000-$23400). */
#define DEST 0x00040000u

static h_result call_ide_read(uint32_t dest, uint32_t lba, uint32_t count)
{
    h_begin_call();
    h_set_a(0, dest);
    h_set_d(0, lba);
    h_set_d(1, count);
    return h_call(h_sym("ide_read"));
}

/* Compare guest memory against the image, read host-side. */
static void expect_sector(uint32_t dest, uint32_t lba)
{
    uint8_t want[512];
    int i;

    if (h_disk_read(lba, want) != 0) {
        t_fail("host-side read of LBA %u failed", lba);
        return;
    }
    for (i = 0; i < 512; i++) {
        uint8_t got = h_peek8(dest + (uint32_t)i);
        if (got != want[i]) {
            t_fail("LBA %u byte %d: expected $%02X got $%02X", lba, i, want[i], got);
            return;
        }
    }
}

/* --- ide.s -------------------------------------------------------------- */

static void t_ide_read_sector0(void)
{
    h_result r;
    if (attach(DISK_IMAGE)) return;

    r = call_ide_read(DEST, 0, 1);
    CHECK_CALL(r);
    CHECK_U32(0u, h_get_d(0));
    expect_sector(DEST, 0);

    /* Byte order must survive the data port: RDSK is a big-endian magic read
     * with move.l, while the FAT BPB below is little-endian read byte by
     * byte. Both come out of this same buffer. */
    CHECK_U32(0x5244534Bu, h_peek32(DEST));
}

static void t_ide_read_multi_sector(void)
{
    h_result r;
    uint32_t i;
    if (attach(DISK_IMAGE)) return;

    r = call_ide_read(DEST, 0, 4);
    CHECK_CALL(r);
    CHECK_U32(0u, h_get_d(0));
    for (i = 0; i < 4; i++)
        expect_sector(DEST + i * 512u, i);
}

static void t_ide_read_deep_lba(void)
{
    h_result r;
    if (attach(DISK_IMAGE)) return;

    /* The FAT boot sector, well past the low blocks. */
    r = call_ide_read(DEST, DISK_PART_START_LBA, 1);
    CHECK_CALL(r);
    CHECK_U32(0u, h_get_d(0));
    expect_sector(DEST, DISK_PART_START_LBA);
    CHECK_U32(0x55u, h_peek8(DEST + 510));
    CHECK_U32(0xAAu, h_peek8(DEST + 511));
}

static void t_ide_reject_zero_count(void)
{
    h_result r;
    if (attach(DISK_IMAGE)) return;

    r = call_ide_read(DEST, 0, 0);
    CHECK_CALL(r);
    CHECK_U32(0xFFFFFFFFu, h_get_d(0));
}

static void t_ide_reject_too_many(void)
{
    h_result r;
    if (attach(DISK_IMAGE)) return;

    r = call_ide_read(DEST, 0, 257);
    CHECK_CALL(r);
    CHECK_U32(0xFFFFFFFFu, h_get_d(0));
}

static void t_ide_read_past_end(void)
{
    h_result r;
    if (attach(DISK_IMAGE)) return;

    r = call_ide_read(DEST, DISK_TOTAL_SECTORS + 10u, 1);
    CHECK_CALL(r);
    CHECK_U32(0xFFFFFFFFu, h_get_d(0));
}

static void t_ide_no_drive(void)
{
    /* Nothing attached: the status register floats to $7F and ide_test_read
     * must report no drive rather than hanging on BSY. */
    h_result r;
    h_begin_call();
    r = h_call(h_sym("ide_test_read"));
    CHECK_CALL(r);
    CHECK_U32(0xFFFFFFFFu, h_get_d(0));
    CHECK_CONTAINS("No drive", h_serial());
}

/* --- partition.s -------------------------------------------------------- */

static void t_rdb_find(void)
{
    h_result r;
    if (attach(DISK_IMAGE)) return;

    h_begin_call();
    r = h_call(h_sym("find_rdb"));
    CHECK_CALL(r);
    CHECK_U32(0u, h_get_d(0));
    CHECK_U32(0x5244534Bu, h_peek32(h_sym("RDB_BUFFER")));
    CHECK_U32(DISK_HEADS,   h_peek32(h_sym("RDB_BUFFER") + 72));
    CHECK_U32(DISK_SECTORS, h_peek32(h_sym("RDB_BUFFER") + 68));
}

static void t_rdb_not_found(void)
{
    h_result r;
    if (attach(DISK_BLANK_IMAGE)) return;

    h_begin_call();
    r = h_call(h_sym("find_rdb"));
    CHECK_CALL(r);
    CHECK_U32(0xFFFFFFFFu, h_get_d(0));
    CHECK_CONTAINS("Not found", h_serial());
}

static void load_rdb(void)
{
    h_result r;
    h_begin_call();
    r = h_call(h_sym("find_rdb"));
    CHECK_CALL(r);
}

static void t_part_load(void)
{
    h_result r;
    if (attach(DISK_IMAGE)) return;
    load_rdb();

    h_begin_call();
    r = h_call(h_sym("load_partition"));
    CHECK_CALL(r);
    CHECK_U32(0u, h_get_d(0));
    CHECK_U32(DISK_PART_START_LBA, h_get_d(1));
    CHECK_U32(DISK_PART_SECTORS,   h_get_d(2));
}

static void t_part_lowcyl_overflow(void)
{
    /* start LBA = LowCyl * Heads * Sectors. With LowCyl * Heads above 65535
     * the old chained mulu.w truncated the intermediate and put the
     * partition roughly 2GB from where it belongs. */
    h_result r;
    if (attach(DISK_BIGCYL_IMAGE)) return;
    load_rdb();

    h_begin_call();
    r = h_call(h_sym("load_partition"));
    CHECK_CALL(r);
    CHECK_U32(0u, h_get_d(0));
    CHECK_U32(DISK_BIGCYL_START_LBA, h_get_d(1));
}

/* --- filesystem.s ------------------------------------------------------- */

static void fat_init(void)
{
    h_result r;
    h_begin_call();
    h_set_d(1, DISK_PART_START_LBA);
    h_set_d(2, DISK_PART_SECTORS);
    r = h_call(h_sym("fat16_init"));
    CHECK_CALL(r);
    CHECK_U32(0u, h_get_d(0));
}

static void t_fat_init(void)
{
    uint32_t v = h_sym("FS_VARS");
    if (attach(DISK_IMAGE)) return;
    fat_init();

    CHECK_U32(DISK_PART_START_LBA,    h_peek32(v + 0));    /* FSV_PARTITION_LBA */
    CHECK_U32(DISK_FAT_BYTES_PER_SEC, h_peek16(v + 4));    /* FSV_BYTES_PER_SEC */
    CHECK_U32(DISK_FAT_SEC_PER_CLUS,  h_peek8 (v + 6));    /* FSV_SEC_PER_CLUS  */
    CHECK_U32(DISK_FAT_RSVD,          h_peek16(v + 8));    /* FSV_RESERVED_SEC  */
    CHECK_U32(DISK_FAT_NUM_FATS,      h_peek8 (v + 10));   /* FSV_NUM_FATS      */
    CHECK_U32(DISK_FAT_ROOT_ENT,      h_peek16(v + 12));   /* FSV_ROOT_ENT_CNT  */
    CHECK_U32(DISK_FAT_SIZE,          h_peek16(v + 14));   /* FSV_FAT_SIZE      */
    CHECK_U32(DISK_FAT_ROOT_START,    h_peek16(v + 16));   /* FSV_ROOT_DIR_START*/
    CHECK_U32(DISK_FAT_ROOT_SECS,     h_peek16(v + 18));   /* FSV_ROOT_DIR_SECS */
    CHECK_U32(DISK_FAT_DATA_START,    h_peek32(v + 20));   /* FSV_DATA_START_SEC*/
}

static void t_fat_init_bad_signature(void)
{
    /* Point the filesystem at a sector that is not a boot sector. */
    h_result r;
    if (attach(DISK_IMAGE)) return;

    h_begin_call();
    h_set_d(1, 0);                       /* LBA 0 is the RDB, not a BPB */
    h_set_d(2, DISK_PART_SECTORS);
    r = h_call(h_sym("fat16_init"));
    CHECK_CALL(r);
    CHECK_U32(0xFFFFFFFFu, h_get_d(0));
    CHECK_CONTAINS("Invalid boot signature", h_serial());
}

static uint32_t find_file(const char *name11)
{
    h_result r;
    h_begin_call();
    h_set_a(0, h_str(name11));
    r = h_call(h_sym("fat16_find_file"));
    CHECK_CALL(r);
    return h_get_d(0);
}

static void t_fat_find_file(void)
{
    static const int chain[] = DISK_SYSBIN_CHAIN;
    if (attach(DISK_IMAGE)) return;
    fat_init();

    CHECK_U32(0u, find_file("SYSTEM  BIN"));
    CHECK_U32((uint32_t)chain[0], h_get_d(1));
    CHECK_U32(DISK_SYSBIN_SIZE,   h_get_d(2));
}

static void t_fat_find_skips_decoys(void)
{
    /* The root directory holds a volume label, a deleted entry and another
     * file ahead of SYSTEM.BIN. Finding README proves the scan walks past
     * the first two rather than stopping or matching them. */
    if (attach(DISK_IMAGE)) return;
    fat_init();
    CHECK_U32(0u, find_file("README  TXT"));
}

static void t_fat_find_missing(void)
{
    if (attach(DISK_IMAGE)) return;
    fat_init();
    CHECK_U32(0xFFFFFFFFu, find_file("NOSUCH  BIN"));
    CHECK_CONTAINS("not found", h_serial());
}

static void t_fat_cluster_chain(void)
{
    static const int chain[] = DISK_SYSBIN_CHAIN;
    int i;
    if (attach(DISK_IMAGE)) return;
    fat_init();

    for (i = 0; i < DISK_SYSBIN_CLUSTERS - 1; i++) {
        h_result r;
        h_begin_call();
        h_set_d(0, (uint32_t)chain[i]);
        r = h_call(h_sym("fat16_get_next_cluster"));
        CHECK_CALL(r);
        if (h_get_d(0) != (uint32_t)chain[i + 1]) {
            t_fail("cluster %d: expected next %d got %u",
                   chain[i], chain[i + 1], h_get_d(0));
            return;
        }
    }
    {
        h_result r;
        h_begin_call();
        h_set_d(0, (uint32_t)chain[DISK_SYSBIN_CLUSTERS - 1]);
        r = h_call(h_sym("fat16_get_next_cluster"));
        CHECK_CALL(r);
        CHECK(h_get_d(0) >= 0xFFF8u,
              "last cluster should be end-of-chain, got $%04X", h_get_d(0));
    }
}

static void t_fat_read_cluster(void)
{
    /* Read the fourth cluster of the chain and check it holds the fourth
     * chunk of the file - not the fourth chunk of the disk. */
    static const int chain[] = DISK_SYSBIN_CHAIN;
    const int idx = 3;
    h_result r;
    int i;

    if (attach(DISK_IMAGE)) return;
    fat_init();

    h_begin_call();
    h_set_a(0, DEST);
    h_set_d(0, (uint32_t)chain[idx]);
    r = h_call(h_sym("fat16_read_cluster"));
    CHECK_CALL(r);
    CHECK_U32(0u, h_get_d(0));

    for (i = 0; i < 512 * DISK_FAT_SEC_PER_CLUS; i++) {
        int off = idx * 512 * DISK_FAT_SEC_PER_CLUS + i;
        uint8_t want = DISK_SYSBIN_BYTE(off);
        uint8_t got  = h_peek8(DEST + (uint32_t)i);
        if (got != want) {
            t_fail("cluster %d byte %d: expected $%02X got $%02X",
                   chain[idx], i, want, got);
            return;
        }
    }
}

static void t_fat_cache_reloads(void)
{
    /* fat16_get_next_cluster caches one FAT sector. SYSTEM.BIN's whole chain
     * lives in FAT sector 0, so alternating with a cluster from another
     * sector is the only thing that makes the cache tag do any work. */
    static const int chain[] = DISK_SYSBIN_CHAIN;
    h_result r;
    int i;

    if (attach(DISK_IMAGE)) return;
    fat_init();

    CHECK(DISK_README_FAT_SECTOR != 0,
          "README cluster should not share FAT sector 0 with the chain");

    for (i = 0; i < 2; i++) {
        h_begin_call();
        h_set_d(0, (uint32_t)chain[0]);
        r = h_call(h_sym("fat16_get_next_cluster"));
        CHECK_CALL(r);
        CHECK_U32((uint32_t)chain[1], h_get_d(0));

        h_begin_call();
        h_set_d(0, DISK_README_CLUSTER);
        r = h_call(h_sym("fat16_get_next_cluster"));
        CHECK_CALL(r);
        CHECK(h_get_d(0) >= 0xFFF8u,
              "README is one cluster, expected end-of-chain, got $%04X",
              h_get_d(0));
    }
}

/* --- the whole path ----------------------------------------------------- */

static void t_load_system_bin(void)
{
    h_result r;
    int i;

    if (attach(DISK_IMAGE)) return;

    h_begin_call();
    h_set_d(1, DISK_PART_START_LBA);
    h_set_d(2, DISK_PART_SECTORS);
    r = h_call(h_sym("load_system_bin"));
    CHECK_CALL(r);
    CHECK_U32(0u, h_get_d(0));
    CHECK_U32(DISK_SYSBIN_SIZE, h_get_d(1));

    for (i = 0; i < DISK_SYSBIN_SIZE; i++) {
        uint8_t want = DISK_SYSBIN_BYTE(i);
        uint8_t got  = h_peek8(h_sym("KERNEL_LOAD_ADDR") + (uint32_t)i);
        if (got != want) {
            t_fail("SYSTEM.BIN byte %d: expected $%02X got $%02X (cluster %d)",
                   i, want, got, i / (512 * DISK_FAT_SEC_PER_CLUS));
            return;
        }
    }
}

static void t_boot_path_end_to_end(void)
{
    /* find_rdb -> load_partition -> load_system_bin, chained exactly as
     * bootstrap.s does it, with the LBA and size flowing through registers. */
    h_result r;

    if (attach(DISK_IMAGE)) return;

    h_begin_call();
    r = h_call(h_sym("find_rdb"));
    CHECK_CALL(r);
    CHECK_U32(0u, h_get_d(0));

    h_begin_call();
    r = h_call(h_sym("load_partition"));
    CHECK_CALL(r);
    CHECK_U32(0u, h_get_d(0));

    {
        uint32_t lba = h_get_d(1), size = h_get_d(2);
        h_begin_call();
        h_set_d(1, lba);
        h_set_d(2, size);
        r = h_call(h_sym("load_system_bin"));
        CHECK_CALL(r);
        CHECK_U32(0u, h_get_d(0));
        CHECK_U32(DISK_SYSBIN_SIZE, h_get_d(1));
    }
    CHECK_U32(DISK_SYSBIN_BYTE(0), h_peek8(h_sym("KERNEL_LOAD_ADDR")));
    CHECK_U32(DISK_SYSBIN_BYTE(4999),
              h_peek8(h_sym("KERNEL_LOAD_ADDR") + DISK_SYSBIN_SIZE - 1));
}

/*
 * The boot partition may be ext2. Same chain as above, different image: the
 * ROM tries ext2 first - its magic number is a far better test than FAT's -
 * and finds "system.bin" in lower case, which ext2 will not fold for it.
 * 20000 bytes at 1KB a block is twelve direct blocks and eight through an
 * indirect one.
 */
static void t_boot_from_ext2(void)
{
    h_result r;
    uint32_t i, lba, size, bad = 0;

    if (attach(DISK_EXT2_BOOT_IMAGE)) return;

    h_begin_call();
    r = h_call(h_sym("find_rdb"));
    CHECK_CALL(r);
    h_begin_call();
    r = h_call(h_sym("load_partition"));
    CHECK_CALL(r);
    CHECK_U32(0u, h_get_d(0));
    lba = h_get_d(1);  size = h_get_d(2);

    h_begin_call();
    h_set_d(1, lba);
    h_set_d(2, size);
    h_set_cycle_budget(200000000);
    r = h_call(h_sym("load_system_bin"));
    CHECK_CALL(r);
    CHECK_U32(0u, h_get_d(0));
    CHECK_U32(DISK_EXT2_BOOT_SIZE, h_get_d(1));
    CHECK_CONTAINS("EXT2: Found SYSTEM.BIN", h_serial());

    for (i = 0; i < DISK_EXT2_BOOT_SIZE; i++)
        if (h_peek8(h_sym("KERNEL_LOAD_ADDR") + i) != DISK_SYSBIN_BYTE(i)) bad++;
    CHECK(bad == 0, "%u of %u bytes wrong", bad, DISK_EXT2_BOOT_SIZE);
}

/* And a FAT16 boot partition is not mistaken for anything else: no ext2
 * error is printed about a filesystem nobody said was there. */
static void t_fat16_boot_says_nothing_of_ext2(void)
{
    h_result r;

    if (attach(DISK_IMAGE)) return;
    h_begin_call();  r = h_call(h_sym("find_rdb"));        CHECK_CALL(r);
    h_begin_call();  r = h_call(h_sym("load_partition"));  CHECK_CALL(r);
    {
        uint32_t lba = h_get_d(1), size = h_get_d(2);
        h_begin_call();
        h_set_d(1, lba);  h_set_d(2, size);
        r = h_call(h_sym("load_system_bin"));
        CHECK_CALL(r);
        CHECK_U32(0u, h_get_d(0));
    }
    CHECK(strstr(h_serial(), "EXT2") == NULL, "ext2 chatter on a FAT16 boot: %s", h_serial());
}

/* ------------------------------------------------------------------------ */

static const test_case tests[] = {
    { "ide_read_sector0",      t_ide_read_sector0,      NULL },
    { "ide_read_multi_sector", t_ide_read_multi_sector, NULL },
    { "ide_read_deep_lba",     t_ide_read_deep_lba,     NULL },
    { "ide_reject_zero_count", t_ide_reject_zero_count, NULL },
    { "ide_reject_too_many",   t_ide_reject_too_many,   NULL },
    { "ide_read_past_end",     t_ide_read_past_end,     NULL },
    { "ide_no_drive",          t_ide_no_drive,          NULL },

    { "rdb_find",              t_rdb_find,              NULL },
    { "rdb_not_found",         t_rdb_not_found,         NULL },
    { "part_load",             t_part_load,             NULL },
    { "part_lowcyl_high",      t_part_lowcyl_overflow,  NULL },

    { "fat_init",              t_fat_init,              NULL },
    { "fat_init_bad_signature",t_fat_init_bad_signature,NULL },
    { "fat_find_file",         t_fat_find_file,         NULL },
    { "fat_find_skips_decoys", t_fat_find_skips_decoys, NULL },
    { "fat_find_missing",      t_fat_find_missing,      NULL },
    { "fat_cluster_chain",     t_fat_cluster_chain,     NULL },
    { "fat_read_cluster",      t_fat_read_cluster,      NULL },
    { "fat_cache_reloads",     t_fat_cache_reloads,     NULL },

    { "load_system_bin",       t_load_system_bin,       NULL },
    { "boot_path_end_to_end",  t_boot_path_end_to_end,  NULL },
    { "boot_from_ext2",        t_boot_from_ext2,        NULL },
    { "fat16_boot_no_ext2_noise", t_fat16_boot_says_nothing_of_ext2, NULL },
};

const test_suite disk_suite = { "disk", tests, sizeof tests / sizeof tests[0] };
