/*
 * test_kblk.c - blk.c, ide.c and bcache.c: disks, partitions, the cache
 *
 * Against two-part.img (tests/mkdisk.py): an RDB whose partition list is
 * NOT at LBA 1 and has two entries chained by PART_NEXT, a FAT16 volume,
 * a second partition where every block holds its own number - so a read
 * that lands in the wrong place says where it landed - and a third, ext2.
 */
#include "protocol.h"
#include "disk_layout.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* struct blkdev: name, read, present, hw, write, blocks */
#define BD_NAME    0
#define BD_WRITE   16
#define BD_BLOCKS  20

static const char *g_dir = "build";
void t_kblk_set_disk_dir(const char *dir) { g_dir = dir; }

static uint32_t kcall(const char *name, int nargs, const uint32_t *args)
{
    h_result r;
    int i;

    h_begin_call();
    for (i = nargs - 1; i >= 0; i--)
        h_push32(args[i]);
    r = h_call(h_sym(name));
    CHECK_CALL(r);
    return h_get_d(0);
}

static int attach(void)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", g_dir, DISK2_IMAGE);
    return h_attach_disk(path);
}

/* The cache sizes itself from free memory, so how much there is decides
 * how big it is: 256KB of heap is a cache of the minimum, 64 blocks. */
static void give_memory(uint32_t bytes)
{
    uint8_t map[24];
    uint32_t a[2], base = h_kernel_heap(0x40000u);

    memset(map, 0, sizeof map);
    map[0] = (uint8_t)(base >> 24); map[1] = (uint8_t)(base >> 16);
    map[4] = (uint8_t)(bytes >> 24); map[5] = (uint8_t)(bytes >> 16); map[6] = (uint8_t)(bytes >> 8);
    map[9] = 2;                                         /* MEM_TYPE_FAST */
    a[0] = h_alloc(map, sizeof map); a[1] = 0;
    kcall("kernel:_mem_init", 2, a);
}

static int setup_with(uint32_t heap_bytes)
{
    if (attach()) { t_fail("no disk image"); return -1; }
    give_memory(heap_bytes);
    kcall("kernel:_ser_init", 0, NULL);
    kcall("kernel:_blk_init", 0, NULL);
    kcall("kernel:_bc_init", 0, NULL);
    return 0;
}

static int setup(void) { return setup_with(0x40000u); }

static uint32_t cache_blocks(void) { return h_peek32(h_sym("kernel:_bc_blocks")); }

static uint32_t find(const char *name)
{
    uint32_t a = h_str(name);
    return kcall("kernel:_blk_find", 1, &a);
}

static uint32_t scratch(uint32_t bytes)
{
    static uint8_t zero[8192];
    return h_alloc(zero, bytes);
}

static int32_t rd(const char *fn, uint32_t dev, uint32_t lba, uint32_t n, uint32_t buf)
{
    uint32_t a[4]; a[0] = dev; a[1] = lba; a[2] = n; a[3] = buf;
    return (int32_t)kcall(fn, 4, a);
}

/* --- disks and partitions -------------------------------------------------- */

static void t_disk_is_found_and_sized(void)
{
    uint32_t disk;

    if (setup()) return;
    disk = find("ide0");
    CHECK(disk != 0, "no ide0");
    if (!disk) return;
    CHECK_U32(DISK2_TOTAL_SECTORS, h_peek32(disk + BD_BLOCKS));    /* from IDENTIFY */
    CHECK(h_peek32(disk + BD_WRITE) != 0, "ide0 cannot be written");
}

/* RDB_PARTLIST says where the first PART block is, PART_NEXT where the rest
 * are. The ROM's boot path reads LBA 1 and stops; here the list is at LBA 3
 * and has three entries. */
static void t_partition_list_is_followed(void)
{
    uint32_t p0, p1;

    if (setup()) return;
    p0 = find("ide0p0");
    p1 = find("ide0p1");
    CHECK(p0 != 0, "first partition not found - is LBA 1 hardcoded?");
    CHECK(p1 != 0, "second partition not found - is PART_NEXT followed?");
    CHECK(find("ide0p2") != 0, "third partition not found");
    CHECK_U32(0, find("ide0p3"));
    if (!p0 || !p1) return;
    CHECK_U32(DISK2_P1_SIZE, h_peek32(p0 + BD_BLOCKS));
    CHECK_U32(DISK2_P2_SIZE, h_peek32(p1 + BD_BLOCKS));
}

static void t_partition_is_a_window(void)
{
    uint32_t p1, buf = scratch(1024);

    if (setup()) return;
    p1 = find("ide0p1");
    if (!p1) { t_fail("no ide0p1"); return; }

    /* Block 5 of the partition, not block 5 of the disk. */
    CHECK_U32(0, (uint32_t)rd("kernel:_blk_read", p1, 5, 2, buf));
    CHECK_U32(5, h_peek32(buf));
    CHECK_U32(5, h_peek32(buf + 508));
    CHECK_U32(6, h_peek32(buf + 512));
}

/* The end of a partition is the start of somebody else's data. */
static void t_partition_bounds_are_enforced(void)
{
    uint32_t p0, p1, buf = scratch(1024);
    unsigned before;

    if (setup()) return;
    p0 = find("ide0p0");  p1 = find("ide0p1");
    if (!p0 || !p1) { t_fail("partitions missing"); return; }
    before = h_disk_commands();

    CHECK(rd("kernel:_blk_read", p0, DISK2_P1_SIZE, 1, buf) != 0, "read past the end");
    CHECK(rd("kernel:_blk_read", p0, DISK2_P1_SIZE - 1, 2, buf) != 0, "read straddling the end");
    CHECK(rd("kernel:_blk_read", p1, 0xFFFFFFF0u, 0x20, buf) != 0, "lba + count wrapped round");
    CHECK(rd("kernel:_blk_write", p0, DISK2_P1_SIZE - 1, 2, buf) != 0, "write straddling the end");
    CHECK_U32(before, h_disk_commands());               /* refused before the drive heard */

    CHECK_U32(0, (uint32_t)rd("kernel:_blk_read", p0, DISK2_P1_SIZE - 1, 1, buf));
}

static void t_writes_reach_the_disk(void)
{
    uint32_t p1, out = scratch(1024), in = scratch(1024), i;
    uint8_t sector[512];

    if (setup()) return;
    p1 = find("ide0p1");
    if (!p1) { t_fail("no ide0p1"); return; }

    for (i = 0; i < 1024; i++) h_poke8(out + i, (uint8_t)(i * 7 + 1));
    CHECK_U32(0, (uint32_t)rd("kernel:_blk_write", p1, 10, 2, out));
    CHECK_U32(2, h_disk_sectors_written());

    /* On the platter, at the partition's offset, in memory order. */
    CHECK_U32(0, (uint32_t)h_disk_read(DISK2_P2_START + 11, sector));
    CHECK_U32((uint8_t)(512 * 7 + 1), sector[0]);
    CHECK_U32((uint8_t)(513 * 7 + 1), sector[1]);

    CHECK_U32(0, (uint32_t)rd("kernel:_blk_read", p1, 10, 2, in));
    for (i = 0; i < 1024; i++)
        if (h_peek8(in + i) != (uint8_t)(i * 7 + 1)) { t_fail("read back differs at %u", i); return; }

    /* The neighbours are untouched. */
    CHECK_U32(0, (uint32_t)rd("kernel:_blk_read", p1, 9, 1, in));
    CHECK_U32(9, h_peek32(in));
    CHECK_U32(0, (uint32_t)rd("kernel:_blk_read", p1, 12, 1, in));
    CHECK_U32(12, h_peek32(in));
}

/* --- the block cache ------------------------------------------------------- */

/* Gayle's IDE is PIO: every word of every sector goes through the CPU. A
 * block read twice should be a cost paid once. */
static void t_second_read_costs_nothing(void)
{
    uint32_t p1, buf = scratch(512);
    unsigned before;

    if (setup()) return;
    p1 = find("ide0p1");
    before = h_disk_sectors_read();

    CHECK_U32(0, (uint32_t)rd("kernel:_bc_read", p1, 40, 1, buf));
    CHECK_U32(40, h_peek32(buf));
    CHECK_U32(before + 1, h_disk_sectors_read());

    h_poke32(buf, 0);
    CHECK_U32(0, (uint32_t)rd("kernel:_bc_read", p1, 40, 1, buf));
    CHECK_U32(40, h_peek32(buf));
    CHECK_U32(before + 1, h_disk_sectors_read());       /* the drive never heard */
}

/* What hurts is the command, not the sector: eight blocks missing in a row
 * are one READ of eight, not eight READs of one. */
static void t_a_run_of_misses_is_one_command(void)
{
    uint32_t p1, buf = scratch(8 * 512), i;
    unsigned cmds, sectors;

    if (setup()) return;
    p1 = find("ide0p1");
    cmds = h_disk_commands();  sectors = h_disk_sectors_read();

    CHECK_U32(0, (uint32_t)rd("kernel:_bc_read", p1, 100, 8, buf));
    CHECK_U32(cmds + 1, h_disk_commands());
    CHECK_U32(sectors + 8, h_disk_sectors_read());
    for (i = 0; i < 8; i++)
        CHECK_U32(100 + i, h_peek32(buf + i * 512));
}

/* A hit in the middle of a request splits it; the cached block is not
 * fetched again and is not replaced by a stale one. */
static void t_hits_in_the_middle_are_not_refetched(void)
{
    uint32_t p1, buf = scratch(8 * 512), i;
    unsigned sectors;

    if (setup()) return;
    p1 = find("ide0p1");
    rd("kernel:_bc_read", p1, 203, 1, buf);
    sectors = h_disk_sectors_read();

    CHECK_U32(0, (uint32_t)rd("kernel:_bc_read", p1, 200, 8, buf));
    CHECK_U32(sectors + 7, h_disk_sectors_read());
    for (i = 0; i < 8; i++)
        CHECK_U32(200 + i, h_peek32(buf + i * 512));
}

/* Two devices, same block number, different blocks. */
static void t_cache_keys_on_the_device(void)
{
    uint32_t p0, p1, a = scratch(512), b = scratch(512);

    if (setup()) return;
    p0 = find("ide0p0");  p1 = find("ide0p1");
    rd("kernel:_bc_read", p1, 0, 1, a);
    rd("kernel:_bc_read", p0, 0, 1, b);
    CHECK_U32(0, h_peek32(a));                          /* p1's block 0 is full of zeros... */
    CHECK(h_peek8(b + 510) == 0x55 && h_peek8(b + 511) == 0xAA,
          "p0 block 0 is not the FAT boot sector: the cache confused the devices");
}

/* Write-through: on the disk before bc_write returns, and in the cache. */
static void t_cached_writes_are_written_through(void)
{
    uint32_t p1, out = scratch(512), in = scratch(512);
    uint8_t sector[512];
    unsigned reads;

    if (setup()) return;
    p1 = find("ide0p1");
    rd("kernel:_bc_read", p1, 77, 1, in);               /* cached, old contents */
    h_poke32(out, 0xFEEDFACEu);

    CHECK_U32(0, (uint32_t)rd("kernel:_bc_write", p1, 77, 1, out));
    h_disk_read(DISK2_P2_START + 77, sector);
    CHECK(sector[0] == 0xFE && sector[3] == 0xCE, "bc_write returned before the disk had it");

    reads = h_disk_sectors_read();
    CHECK_U32(0, (uint32_t)rd("kernel:_bc_read", p1, 77, 1, in));
    CHECK_U32(0xFEEDFACEu, h_peek32(in));               /* not the stale copy */
    CHECK_U32(reads, h_disk_sectors_read());            /* and from the cache */
}

/* More blocks than the cache holds: old ones go, nothing is corrupted, and
 * what is asked for is always what is returned. */
static void t_cache_survives_being_too_small(void)
{
    uint32_t p1, buf = scratch(512), i, hits, size, n, recent;

    if (setup()) return;
    p1 = find("ide0p1");
    size = cache_blocks();
    CHECK(size >= 64 && size < 600, "cache of %u blocks from 256KB of heap", size);
    n = size + 40;                      /* more than it holds */
    recent = size / 2;

    for (i = 0; i < n; i++)
        if (rd("kernel:_bc_read", p1, i * 3, 1, buf) != 0 || h_peek32(buf) != i * 3) {
            t_fail("read of block %u returned block %u", i * 3, h_peek32(buf));
            return;
        }
    hits = h_peek32(h_sym("kernel:_bc_hits"));

    /* The recent ones again: all still there. (Revisiting everything in the
     * same order is the one pattern LRU can do nothing with - each block is
     * pushed out just before its turn.) */
    for (i = n - recent; i < n; i++)
        if (rd("kernel:_bc_read", p1, i * 3, 1, buf) != 0 || h_peek32(buf) != i * 3) {
            t_fail("re-read of block %u returned block %u", i * 3, h_peek32(buf));
            return;
        }
    CHECK_U32(hits + recent, h_peek32(h_sym("kernel:_bc_hits")));

    /* The oldest is gone - and comes back right, at the cost of a read. */
    {
        unsigned before = h_disk_sectors_read();
        if (rd("kernel:_bc_read", p1, 0, 1, buf) != 0 || h_peek32(buf) != 0)
            t_fail("evicted block 0 came back wrong");
        CHECK_U32(before + 1, h_disk_sectors_read());
    }
}

/* What is touched stays. Least recently USED, not least recently loaded: a
 * block read early and often must outlive blocks read later and once. */
static void t_eviction_is_by_use_not_by_age(void)
{
    uint32_t p1, buf = scratch(512), i, size, n;
    unsigned before;

    if (setup()) return;
    p1 = find("ide0p1");
    size = cache_blocks();
    n = size + 40;
    before = h_disk_sectors_read();

    rd("kernel:_bc_read", p1, 900, 1, buf);             /* loaded first */
    for (i = 0; i < n; i++) {
        rd("kernel:_bc_read", p1, i, 1, buf);
        if ((i & 7) == 0) {
            rd("kernel:_bc_read", p1, 900, 1, buf);     /* ...and kept in use */
            CHECK_U32(900, h_peek32(buf));
        }
    }

    /* Counted, not just looked for at the end: a cache that evicted block
     * 900 by age would quietly read it back in every so often, and it would
     * be sitting there looking innocent when the loop finished. One read of
     * each distinct block is all the disk should have been asked for. */
    CHECK_U32(before + n + 1, h_disk_sectors_read());
}

/* The cache is sized from what is free: more memory, more cache. */
static void t_cache_grows_with_memory(void)
{
    uint32_t small, large;

    if (setup_with(0x40000u)) return;                   /* 256KB */
    small = cache_blocks();
    h_reset();
    if (setup_with(0x80000u - 0x10000u)) return;        /* 448KB */
    large = cache_blocks();

    CHECK(small >= 64, "minimum not honoured: %u", small);
    CHECK(large > small, "448KB of heap gave %u blocks, 256KB gave %u", large, small);
}

/* No memory at all: the cache is off, and everything still works. */
static void t_no_memory_means_no_cache_not_no_disk(void)
{
    uint32_t p1, buf = scratch(512);
    unsigned before;

    if (setup_with(0x2000u)) return;                    /* 8KB: not enough */
    CHECK_U32(0, cache_blocks());
    p1 = find("ide0p1");
    before = h_disk_sectors_read();

    CHECK_U32(0, (uint32_t)rd("kernel:_bc_read", p1, 40, 1, buf));
    CHECK_U32(40, h_peek32(buf));
    CHECK_U32(0, (uint32_t)rd("kernel:_bc_read", p1, 40, 1, buf));
    CHECK_U32(before + 2, h_disk_sectors_read());       /* both went to the disk */

    h_poke32(buf, 0xABCD1234u);
    CHECK_U32(0, (uint32_t)rd("kernel:_bc_write", p1, 40, 1, buf));
    CHECK_U32(0, (uint32_t)rd("kernel:_bc_read", p1, 40, 1, buf));
    CHECK_U32(0xABCD1234u, h_peek32(buf));
}

static const test_case tests[] = {
    { "disk_found_and_sized",   t_disk_is_found_and_sized,          NULL },
    { "partition_list_followed", t_partition_list_is_followed,      NULL },
    { "partition_is_a_window",  t_partition_is_a_window,            NULL },
    { "partition_bounds",       t_partition_bounds_are_enforced,    NULL },
    { "writes_reach_the_disk",  t_writes_reach_the_disk,            NULL },
    { "second_read_is_free",    t_second_read_costs_nothing,        NULL },
    { "misses_coalesce",        t_a_run_of_misses_is_one_command,   NULL },
    { "hit_splits_a_request",   t_hits_in_the_middle_are_not_refetched, NULL },
    { "cache_keys_on_device",   t_cache_keys_on_the_device,         NULL },
    { "write_through",          t_cached_writes_are_written_through, NULL },
    { "cache_eviction",         t_cache_survives_being_too_small,   NULL },
    { "eviction_by_use",        t_eviction_is_by_use_not_by_age,    NULL },
    { "cache_grows_with_memory", t_cache_grows_with_memory,         NULL },
    { "no_memory_no_cache",     t_no_memory_means_no_cache_not_no_disk, NULL },
};

const test_suite kblk_suite = { "kblk", tests, sizeof tests / sizeof tests[0] };
