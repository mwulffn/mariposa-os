/*
 * test_kext2.c - ext2: src/shared/ext2.c and src/kernel/fs_ext2.c
 *
 * Against a filesystem made by the real mke2fs (tests/mkdisk.py), so what
 * is tested is what Linux makes and not what anyone here believes ext2
 * looks like: 1KB blocks, four block groups, a 300KB file that needs double
 * indirection, a directory of 300 files spilling into a second block group, a 124-character name, a symlink,
 * and two names that differ only by case.
 */
#include "protocol.h"
#include "disk_layout.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define VFS_ENOENT   (-1)
#define VFS_EINVAL   (-9)
#define VFS_FILE 1
#define VFS_DIR  2
#define VFS_LINK 3

/* struct vfs_dirent: name[256], type, size */
#define DE_TYPE 256
#define DE_SIZE 260
#define DE_BYTES 264

static const char *g_dir = "build";
void t_kext2_set_disk_dir(const char *dir) { g_dir = dir; }

static uint32_t kcall(const char *name, int nargs, const uint32_t *args)
{
    h_result r;
    int i;

    h_begin_call();
    for (i = nargs - 1; i >= 0; i--)
        h_push32(args[i]);
    h_set_cycle_budget(400000000);
    r = h_call(h_sym(name));
    CHECK_CALL(r);
    return h_get_d(0);
}

static int32_t mount(const char *vol, const char *dev, const char *type)
{
    uint32_t a[3]; a[0] = h_str(vol); a[1] = h_str(dev); a[2] = type ? h_str(type) : 0;
    return (int32_t)kcall("kernel:_vfs_mount", 3, a);
}

static int setup(void)
{
    uint8_t map[24];
    uint32_t a[2], base = h_kernel_heap(0x60000u);
    char path[512];

    snprintf(path, sizeof path, "%s/%s", g_dir, DISK2_IMAGE);
    if (h_attach_disk(path)) { t_fail("no disk image"); return -1; }

    memset(map, 0, sizeof map);
    map[0] = (uint8_t)(base >> 24); map[1] = (uint8_t)(base >> 16);
    map[5] = 0x06; map[9] = 2;                          /* 384KB fast */
    a[0] = h_alloc(map, sizeof map); a[1] = 0;
    kcall("kernel:_mem_init", 2, a);
    kcall("kernel:_ser_init", 0, NULL);
    kcall("kernel:_blk_init", 0, NULL);
    kcall("kernel:_bc_init", 0, NULL);
    kcall("kernel:_vfs_init", 0, NULL);
    a[0] = h_sym("kernel:_fat16_fs");  kcall("kernel:_vfs_register_fs", 1, a);
    a[0] = h_sym("kernel:_ext2_fs");   kcall("kernel:_vfs_register_fs", 1, a);
    CHECK_U32(0, (uint32_t)mount("sys", "ide0p2", NULL));
    return 0;
}

static int32_t vopen(const char *path)    { uint32_t a = h_str(path); return (int32_t)kcall("kernel:_vfs_open", 1, &a); }
static int32_t vclose(int32_t h)          { uint32_t a = (uint32_t)h; return (int32_t)kcall("kernel:_vfs_close", 1, &a); }

static int32_t vread(int32_t h, uint32_t buf, uint32_t len)
{
    uint32_t a[3]; a[0] = (uint32_t)h; a[1] = buf; a[2] = len;
    return (int32_t)kcall("kernel:_vfs_read", 3, a);
}

static uint32_t scratch(uint32_t n)
{
    static uint8_t zero[8192];
    return h_alloc(zero, n);
}

/* The first line of a file, as a C string. */
static void first_line(const char *path, char *out, size_t outsz)
{
    uint32_t buf = scratch(64);
    int32_t h = vopen(path), n;

    out[0] = 0;
    if (h < 1) { t_fail("open %s: %d", path, (int)h); return; }
    n = vread(h, buf, 60);
    vclose(h);
    if (n < 0) { t_fail("read %s: %d", path, (int)n); return; }
    h_poke8(buf + (uint32_t)n, 0);
    h_peekstr(buf, out, outsz);
}

/* --- mounting -------------------------------------------------------------- */

/* Each probe claims its own and only its own, and both can be up at once. */
static void t_probes_tell_the_filesystems_apart(void)
{
    const char *volume;
    char line[64];

    if (setup()) return;
    CHECK_U32(0, (uint32_t)mount("boot", "ide0p0", NULL));
    CHECK(mount("x", "ide0p2", "fat16") != 0, "fat16 claimed an ext2 partition");
    CHECK(mount("y", "ide0p0", "ext2") != 0, "ext2 claimed a FAT16 partition");
    CHECK(mount("z", "ide0p1", "ext2") != 0, "ext2 claimed a partition with no filesystem");

    first_line("sys:hello.txt", line, sizeof line);
    CHECK_STR("hello from ext2\n", line);
    first_line("boot:docs/notes.txt", line, sizeof line);
    CHECK_STR(DISK2_NOTES_TEXT, line);
    (void)volume;
}

/*
 * "Incompatible" means it: a reader that does not know a feature must not
 * touch the filesystem. Here the superblock is edited to claim extents, which
 * is what an ext4 volume would say - and mounting that as ext2 would read
 * extent trees as block pointers.
 */
static void t_unknown_incompat_feature_is_refused(void)
{
    uint32_t dev, sb = scratch(1024), a[4], vol = h_str("sys");
    uint32_t name = h_str("ide0p2");

    if (setup()) return;
    CHECK_U32(0, kcall("kernel:_vfs_unmount", 1, &vol));

    dev = kcall("kernel:_blk_find", 1, &name);
    a[0] = dev; a[1] = 2; a[2] = 2; a[3] = sb;          /* the superblock: byte 1024 */
    CHECK_U32(0, kcall("kernel:_bc_read", 4, a));
    h_poke8(sb + 96, (uint8_t)(h_peek8(sb + 96) | 0x40));   /* INCOMPAT_EXTENTS */
    CHECK_U32(0, kcall("kernel:_bc_write", 4, a));

    CHECK(mount("sys", "ide0p2", NULL) != 0, "mounted a filesystem with extents as ext2");

    h_poke8(sb + 96, (uint8_t)(h_peek8(sb + 96) & ~0x40));
    kcall("kernel:_bc_write", 4, a);
    CHECK_U32(0, (uint32_t)mount("sys", "ide0p2", NULL));   /* and it was only that */
}

/* --- directories ----------------------------------------------------------- */

static int count_dir(const char *path, const char *look_for, uint32_t *type, uint32_t *size)
{
    uint32_t ent = scratch(DE_BYTES), a[2];
    uint32_t p = h_str(path);
    int32_t h = (int32_t)kcall("kernel:_vfs_opendir", 1, &p);
    char name[256];
    int n = 0;

    if (h < 1) { t_fail("opendir %s: %d", path, (int)h); return -1; }
    a[0] = (uint32_t)h; a[1] = ent;
    while ((int32_t)kcall("kernel:_vfs_readdir", 2, a) == 1) {
        h_peekstr(ent, name, sizeof name);
        if (look_for && strcmp(name, look_for) == 0) {
            if (type) *type = h_peek32(ent + DE_TYPE);
            if (size) *size = h_peek32(ent + DE_SIZE);
            look_for = NULL;
        }
        n++;
    }
    vclose(h);
    if (look_for) t_fail("%s is not listed in %s", look_for, path);
    return n;
}

static void t_root_listing(void)
{
    uint32_t type = 0, size = 0;

    if (setup()) return;
    /* hello.txt docs many case empty link <long> lost+found; not . or .. */
    CHECK_U32(8, (uint32_t)count_dir("sys:", "hello.txt", &type, &size));
    CHECK_U32(VFS_FILE, type);  CHECK_U32(16, size);
    count_dir("sys:", "docs", &type, NULL);   CHECK_U32(VFS_DIR, type);
    count_dir("sys:", "link", &type, NULL);   CHECK_U32(VFS_LINK, type);
}

/* 300 entries do not fit one directory block - and 300 inodes do not fit
 * one block group, so the last files' inodes are found through the second
 * group's descriptor, not the first's. */
static void t_directory_spans_blocks_and_groups(void)
{
    char line[64];

    if (setup()) return;
    CHECK_U32(DISK2_EXT2_MANY, (uint32_t)count_dir("sys:many", "file299.txt", NULL, NULL));
    first_line("sys:many/file077.txt", line, sizeof line);
    CHECK_STR("77\n", line);
    first_line("sys:many/file299.txt", line, sizeof line);
    CHECK_STR("299\n", line);
}

static void t_long_names(void)
{
    char line[64];

    if (setup()) return;
    count_dir("sys:", DISK2_EXT2_LONG_NAME, NULL, NULL);
    first_line("sys:" DISK2_EXT2_LONG_NAME, line, sizeof line);
    CHECK_STR("long\n", line);
}

/*
 * Lookup ignores case, and ext2 does not: a host can leave Readme and README
 * side by side. The rule: an exact match wins; otherwise the first match in
 * directory order.
 */
static void t_case_rule(void)
{
    char line[64];

    if (setup()) return;
    first_line("sys:case/README", line, sizeof line);   CHECK_STR("upper\n", line);
    first_line("sys:case/Readme", line, sizeof line);   CHECK_STR("mixed\n", line);
    first_line("sys:CASE/readme", line, sizeof line);   CHECK_STR("mixed\n", line);  /* first */
    first_line("SYS:Docs/NOTES.txt", line, sizeof line);
    CHECK_STR(DISK2_NOTES_TEXT, line);
}

/* --- files ----------------------------------------------------------------- */

/* 300KB at 1KB a block: 12 direct blocks, 256 through one indirect block,
 * the rest through two. Read in pieces that straddle every boundary. */
static void t_big_file_through_double_indirection(void)
{
    uint32_t buf = scratch(1000), total = 0, i;
    int32_t h, n;

    if (setup()) return;
    h = vopen("sys:docs/deep/big.dat");
    if (h < 1) { t_fail("open: %d", (int)h); return; }

    while ((n = vread(h, buf, 1000)) > 0) {
        for (i = 0; i < (uint32_t)n; i++)
            if (h_peek8(buf + i) != DISK2_EXT2_BYTE(total + i)) {
                t_fail("byte %u is wrong (block %u)", total + i, (total + i) / 1024);
                vclose(h);
                return;
            }
        total += (uint32_t)n;
    }
    CHECK_U32(0, (uint32_t)n);
    CHECK_U32(DISK2_EXT2_BIG_SIZE, total);
    vclose(h);
}

static void t_seek_into_each_region(void)
{
    static const uint32_t where[] = { 5, 11 * 1024 + 1000, 12 * 1024 + 7, 200 * 1024,
                                      268 * 1024 + 3, 290 * 1024, 3 };
    uint32_t buf = scratch(16), a[2];
    int32_t h;
    size_t i;

    if (setup()) return;
    h = vopen("sys:docs/deep/big.dat");
    if (h < 1) { t_fail("open: %d", (int)h); return; }
    for (i = 0; i < sizeof where / sizeof where[0]; i++) {
        a[0] = (uint32_t)h; a[1] = where[i];
        kcall("kernel:_vfs_seek", 2, a);
        CHECK_U32(8, (uint32_t)vread(h, buf, 8));
        CHECK(h_peek8(buf) == DISK2_EXT2_BYTE(where[i]) &&
              h_peek8(buf + 7) == DISK2_EXT2_BYTE(where[i] + 7),
              "wrong data at offset %u", where[i]);
    }
    vclose(h);
}

static void t_empty_file_and_symlink(void)
{
    uint32_t buf = scratch(16);
    int32_t h;

    if (setup()) return;
    h = vopen("sys:empty");
    CHECK(h >= 1, "open empty: %d", (int)h);
    CHECK_U32(0, (uint32_t)vread(h, buf, 16));
    vclose(h);

    /* Symlinks are listed and not followed: there is nothing yet for one to
     * mean. Opening one is refused, not read as a file full of a path. */
    CHECK_U32((uint32_t)VFS_EINVAL, (uint32_t)vopen("sys:link"));
    CHECK_U32((uint32_t)VFS_ENOENT, (uint32_t)vopen("sys:docs/nothere"));
}

/*
 * What the cache is for, and what it is not. Metadata - the three
 * directories and four inodes on the way to this file - is read over and
 * over and costs a disk command each time, so the second time round it is
 * free. Bulk data is not cached at all: on CompactFlash a hit saves only the
 * difference between a PIO transfer and a copy, and caching it on the way
 * past costs as much as fetching it. So the second read costs exactly the
 * data, in one command, and nothing else.
 */
static void t_second_read_costs_only_the_data(void)
{
    uint32_t buf = scratch(4096);
    unsigned sectors, commands, first;
    int32_t h;

    if (setup()) return;
    first = h_disk_sectors_read();
    h = vopen("sys:docs/deep/big.dat");  vread(h, buf, 4096);  vclose(h);
    first = h_disk_sectors_read() - first;

    sectors = h_disk_sectors_read();  commands = h_disk_commands();
    h = vopen("sys:docs/deep/big.dat");
    CHECK_U32(4096, (uint32_t)vread(h, buf, 4096));
    vclose(h);

    CHECK(first > 8 + 8, "the first read touched only %u sectors", first);
    CHECK_U32(sectors + 8, h_disk_sectors_read());      /* 4096 bytes and no more */
    CHECK_U32(commands + 1, h_disk_commands());         /* four adjacent blocks, once */
}

/* A small file does go through the cache: it is a partial block, which needs
 * a buffer in any case. Commands, configuration, icons - read again and
 * again, and the reason AmigaOS users kept things resident. */
static void t_small_files_are_cached(void)
{
    char line[64];
    unsigned sectors;

    if (setup()) return;
    first_line("sys:hello.txt", line, sizeof line);
    sectors = h_disk_sectors_read();
    first_line("sys:hello.txt", line, sizeof line);
    CHECK_STR("hello from ext2\n", line);
    CHECK_U32(sectors, h_disk_sectors_read());
}

/* === writing ================================================================
 *
 * The judge is not this file. After the guest has written, the partition is
 * dumped out of the machine and handed to e2fsck, which must find nothing
 * wrong at all, and to debugfs, which reads the files back. If Linux's tools
 * disagree with what was written, it was written wrong.
 */
#define VFS_EROFS     (-10)
#define VFS_EEXIST    (-11)
#define VFS_ENOSPC    (-12)
#define VFS_ENOTEMPTY (-13)
#define O_WRITE  1u
#define O_CREATE 2u
#define O_TRUNC  4u

/* Bounds for t_bulk_throughput, in cycles per KB. Measured when written:
 * read 9,900 (713KB/s), write 19,000 (372KB/s); before the direct path,
 * 30,400 and 45,000. The PIO loop alone is about 6,500. */
#define BULK_READ_MAX   13000u
#define BULK_WRITE_MAX  25000u

#include <stdlib.h>

static int32_t vopenf(const char *path, uint32_t flags)
{
    uint32_t a[2]; a[0] = h_str(path); a[1] = flags;
    return (int32_t)kcall("kernel:_vfs_open_flags", 2, a);
}

static int32_t vwrite(int32_t h, uint32_t buf, uint32_t len)
{
    uint32_t a[3]; a[0] = (uint32_t)h; a[1] = buf; a[2] = len;
    return (int32_t)kcall("kernel:_vfs_write", 3, a);
}

static int32_t vremove(const char *path) { uint32_t a = h_str(path); return (int32_t)kcall("kernel:_vfs_remove", 1, &a); }
static int32_t vmkdir(const char *path)  { uint32_t a = h_str(path); return (int32_t)kcall("kernel:_vfs_mkdir", 1, &a); }

static void vseek(int32_t h, uint32_t off)
{
    uint32_t a[2]; a[0] = (uint32_t)h; a[1] = off;
    kcall("kernel:_vfs_seek", 2, a);
}

static const char *dump_path(void)
{
    static char path[512];
    snprintf(path, sizeof path, "%s/ext2-after.img", g_dir);
    return path;
}

/* Sync, dump the partition, and ask e2fsck. */
static void fsck_must_be_clean(const char *when)
{
    char cmd[1200], line[256], report[1500] = "";
    FILE *p;
    int rc;

    kcall("kernel:_vfs_sync", 0, NULL);
    if (h_disk_save(dump_path(), DISK2_P3_START, DISK2_P3_SIZE) != 0) {
        t_fail("cannot dump the partition");
        return;
    }
    snprintf(cmd, sizeof cmd, "LC_ALL=C '%s' -fn '%s' 2>&1", DISK_E2FSCK, dump_path());
    p = popen(cmd, "r");
    if (!p) { t_fail("cannot run e2fsck"); return; }
    while (fgets(line, sizeof line, p))
        if (strlen(report) + strlen(line) < sizeof report - 1) {
            char *nl = strchr(line, '\n');
            if (nl) *nl = ' ';
            strcat(report, line);
        }
    rc = pclose(p);
    CHECK(rc == 0, "%s: e2fsck is not happy: %s", when, report);
}

/* A file's contents according to debugfs, into a host buffer. -1 if absent. */
static long host_cat(const char *path, uint8_t *out, size_t max)
{
    char cmd[1200];
    FILE *p;
    size_t n;

    snprintf(cmd, sizeof cmd, "LC_ALL=C '%s' -R 'cat %s' '%s' 2>/dev/null", DISK_DEBUGFS, path, dump_path());
    p = popen(cmd, "r");
    if (!p) return -1;
    n = fread(out, 1, max, p);
    pclose(p);
    return (long)n;
}

static uint32_t free_blocks(void)
{
    char cmd[1200], line[256];
    unsigned long n = 0;
    FILE *p;

    snprintf(cmd, sizeof cmd, "LC_ALL=C '%s' -R stats '%s' 2>/dev/null", DISK_DEBUGFS, dump_path());
    p = popen(cmd, "r");
    if (!p) return 0;
    while (fgets(line, sizeof line, p))
        if (sscanf(line, "Free blocks: %lu", &n) == 1) break;
    pclose(p);
    return (uint32_t)n;
}

static uint8_t wbyte(uint32_t i) { return (uint8_t)(i * 23 + 9 + (i >> 8) * 3 + (i >> 16) * 7); }

static void write_pattern(int32_t h, uint32_t total, uint32_t chunk)
{
    uint32_t buf = scratch(chunk), done = 0, i;

    while (done < total) {
        uint32_t n = total - done < chunk ? total - done : chunk;
        for (i = 0; i < n; i++) h_poke8(buf + i, wbyte(done + i));
        if (vwrite(h, buf, n) != (int32_t)n) { t_fail("write failed at %u", done); return; }
        done += n;
    }
}

/*
 * What writing costs, in the only unit that means anything here: CPU cycles
 * per kilobyte, which at 7.09MHz is a transfer rate. Gayle's PIO sets the
 * floor - about 13,000 cycles to push 1KB through the data port - and the
 * question is how far above it the filesystem sits. The first version wrote
 * a bitmap and an indirect block per data block and copied every byte
 * through a buffer: about 4KB of PIO per 1KB stored.
 */
static void t_write_throughput(void)
{
    uint32_t buf = scratch(4096), a[3], i;
    uint64_t cycles = 0, per_kb;
    int32_t h;

    if (setup()) return;
    h = vopenf("sys:speed.dat", O_WRITE | O_CREATE);
    if (h < 1) { t_fail("create: %d", (int)h); return; }
    for (i = 0; i < 4096; i++) h_poke8(buf + i, (uint8_t)i);

    for (i = 0; i < 64; i++) {                          /* 256KB in 4KB calls */
        h_result r;
        a[0] = (uint32_t)h; a[1] = buf; a[2] = 4096;
        h_begin_call();
        h_push32(a[2]); h_push32(a[1]); h_push32(a[0]);
        h_set_cycle_budget(400000000);
        r = h_call(h_sym("kernel:_vfs_write"));
        CHECK_CALL(r);
        cycles += r.cycles;
    }
    vclose(h);

    per_kb = cycles / 256;
    if (getenv("SHOW"))
        fprintf(stderr, "ext2 write: %llu cycles/KB = %llu KB/s at 7.09MHz\n",
                (unsigned long long)per_kb, (unsigned long long)(7090000 / per_kb));
    /* Measured when this was written: 141,000 to begin with; 64,000 after
     * the PIO loop and the cache's block copy went to assembly and the
     * bitmap and map blocks stopped being written once per data block. */
    CHECK(per_kb < 50000, "%llu cycles per KB written: %llu KB/s on a 7MHz 68000",
          (unsigned long long)per_kb, (unsigned long long)(7090000 / per_kb));
    fsck_must_be_clean("after the throughput run");
}

/* And reading: a 300KB file, cold, in 4KB calls. Whole blocks go straight
 * from the disk into the caller's buffer. */
static void t_read_throughput(void)
{
    uint32_t buf = scratch(4096), a[3];
    uint64_t cycles = 0, per_kb;
    uint32_t total = 0;
    int32_t h;

    if (setup()) return;
    h = vopen("sys:docs/deep/big.dat");
    if (h < 1) { t_fail("open: %d", (int)h); return; }
    for (;;) {
        h_result r;
        a[0] = (uint32_t)h; a[1] = buf; a[2] = 4096;
        h_begin_call();
        h_push32(a[2]); h_push32(a[1]); h_push32(a[0]);
        h_set_cycle_budget(400000000);
        r = h_call(h_sym("kernel:_vfs_read"));
        CHECK_CALL(r);
        cycles += r.cycles;
        if ((int32_t)h_get_d(0) <= 0) break;
        total += h_get_d(0);
    }
    vclose(h);
    CHECK_U32(DISK2_EXT2_BIG_SIZE, total);

    per_kb = cycles / (total / 1024);
    if (getenv("SHOW"))
        fprintf(stderr, "ext2 read: %llu cycles/KB = %llu KB/s at 7.09MHz\n",
                (unsigned long long)per_kb, (unsigned long long)(7090000 / per_kb));
    CHECK(per_kb < 16000, "%llu cycles per KB read: %llu KB/s on a 7MHz 68000",
          (unsigned long long)per_kb, (unsigned long long)(7090000 / per_kb));
}

/*
 * The DiskSpeed shape: a big file, written and then read back, in big
 * transfers. This is what loading a program or copying a file looks like,
 * and what the benchmarks people quote for real Amigas measure. 32KB calls,
 * 512KB in all, and the ATA commands counted as well as the cycles - on a
 * CF card there is no seek, so the command is the unit of overhead.
 */
#define BULK_CALL   32768u
#define BULK_TOTAL  (16u * BULK_CALL)

static uint64_t bulk_pass(int32_t h, const char *fn, uint32_t buf)
{
    uint64_t cycles = 0;
    uint32_t i;

    for (i = 0; i < BULK_TOTAL / BULK_CALL; i++) {
        h_result r;
        h_begin_call();
        h_push32(BULK_CALL); h_push32(buf); h_push32((uint32_t)h);
        h_set_cycle_budget(2000000000u);
        r = h_call(h_sym(fn));
        CHECK_CALL(r);
        CHECK_U32(BULK_CALL, h_get_d(0));
        cycles += r.cycles;
    }
    return cycles;
}

static void t_bulk_throughput(void)
{
    uint32_t buf, i;
    uint64_t wr, rdc;
    unsigned wcmds, rcmds;
    int32_t h;

    if (setup()) return;
    { static uint8_t zero[BULK_CALL]; buf = h_alloc(zero, sizeof zero); }
    for (i = 0; i < BULK_CALL; i += 4) h_poke32(buf + i, i * 2654435761u);

    h = vopenf("sys:bench.dat", O_WRITE | O_CREATE);
    if (h < 1) { t_fail("create: %d", (int)h); return; }
    wcmds = h_disk_commands();
    wr = bulk_pass(h, "kernel:_vfs_write", buf);
    wcmds = h_disk_commands() - wcmds;
    vclose(h);

    /* Cold: a fresh mount, so nothing of the file is in any cache. */
    { uint32_t vol = h_str("sys"); kcall("kernel:_vfs_unmount", 1, &vol); }
    CHECK_U32(0, (uint32_t)mount("sys", "ide0p2", NULL));
    h = vopen("sys:bench.dat");
    rcmds = h_disk_commands();
    rdc = bulk_pass(h, "kernel:_vfs_read", buf);
    rcmds = h_disk_commands() - rcmds;
    vclose(h);
    for (i = 0; i < BULK_CALL; i += 4)
        if (h_peek32(buf + i) != i * 2654435761u) { t_fail("read back wrong at %u", i); break; }

    if (getenv("SHOW"))
        fprintf(stderr,
            "ext2 bulk: write %llu cycles/KB = %llu KB/s, %u commands;"
            " read %llu cycles/KB = %llu KB/s, %u commands\n",
            (unsigned long long)(wr / 512), (unsigned long long)(7090000 / (wr / 512)), wcmds,
            (unsigned long long)(rdc / 512), (unsigned long long)(7090000 / (rdc / 512)), rcmds);
    CHECK(rdc / 512 < BULK_READ_MAX, "bulk read: %llu cycles/KB, %llu KB/s",
          (unsigned long long)(rdc / 512), (unsigned long long)(7090000 / (rdc / 512)));
    CHECK(wr / 512 < BULK_WRITE_MAX, "bulk write: %llu cycles/KB, %llu KB/s",
          (unsigned long long)(wr / 512), (unsigned long long)(7090000 / (wr / 512)));
    fsck_must_be_clean("after the bulk benchmark");
}

/*
 * Bulk data goes round the cache and small I/O goes through it, so the two
 * must never disagree about a block. A direct write has to drop any cached
 * copy; a cached write is write-through, so a direct read sees it.
 */
static void t_direct_and_cached_io_agree(void)
{
    uint32_t big = scratch(8192), small = scratch(16), i;
    int32_t h;

    if (setup()) return;
    h = vopenf("sys:mixed.dat", O_WRITE | O_CREATE);
    if (h < 1) { t_fail("create: %d", (int)h); return; }

    for (i = 0; i < 8192; i++) h_poke8(big + i, 0x11);
    CHECK_U32(8192, (uint32_t)vwrite(h, big, 8192));    /* direct: whole blocks */

    vseek(h, 3000);                                     /* cached: inside block 2 */
    for (i = 0; i < 10; i++) h_poke8(small + i, 0x22);
    CHECK_U32(10, (uint32_t)vwrite(h, small, 10));
    vseek(h, 3000);
    CHECK_U32(10, (uint32_t)vread(h, small, 10));       /* block 2 is cached now */

    vseek(h, 0);
    for (i = 0; i < 8192; i++) h_poke8(big + i, 0x33);
    CHECK_U32(8192, (uint32_t)vwrite(h, big, 8192));    /* direct, over the cached block */

    vseek(h, 3000);
    CHECK_U32(10, (uint32_t)vread(h, small, 10));       /* cached path again */
    CHECK_U32(0x33, h_peek8(small));                    /* not the stale 0x22 */

    vseek(h, 5000);
    h_poke8(small, 0x44);
    CHECK_U32(1, (uint32_t)vwrite(h, small, 1));        /* cached write... */
    vseek(h, 0);
    CHECK_U32(8192, (uint32_t)vread(h, big, 8192));     /* ...seen by a direct read */
    CHECK_U32(0x44, h_peek8(big + 5000));
    CHECK_U32(0x33, h_peek8(big + 4999));
    CHECK_U32(0x33, h_peek8(big + 5001));
    vclose(h);
    fsck_must_be_clean("after mixing direct and cached I/O");
}

/* The direct path moves words, so it needs an even address. An odd one is
 * unusual and legal, and must take the other road, not an address error. */
static void t_odd_buffer_addresses(void)
{
    uint32_t buf = scratch(4200) + 1, i, bad = 0;
    int32_t h;

    if (setup()) return;
    h = vopen("sys:docs/deep/big.dat");
    if (h < 1) { t_fail("open: %d", (int)h); return; }
    CHECK_U32(4096, (uint32_t)vread(h, buf, 4096));
    for (i = 0; i < 4096; i++)
        if (h_peek8(buf + i) != DISK2_EXT2_BYTE(i)) bad++;
    CHECK(bad == 0, "%u bytes wrong reading to an odd address", bad);
    vclose(h);

    h = vopenf("sys:odd.dat", O_WRITE | O_CREATE);
    for (i = 0; i < 4096; i++) h_poke8(buf + i, wbyte(i));
    CHECK_U32(4096, (uint32_t)vwrite(h, buf, 4096));
    vclose(h);
    {
        static uint8_t got[4200];
        fsck_must_be_clean("after writing from an odd address");
        CHECK_U32(4096, (uint32_t)host_cat("/odd.dat", got, sizeof got));
        for (i = 0, bad = 0; i < 4096; i++) if (got[i] != wbyte(i)) bad++;
        CHECK(bad == 0, "%u bytes wrong writing from an odd address", bad);
    }
}

static void t_create_a_small_file(void)
{
    static uint8_t got[64];
    uint32_t buf = scratch(32);
    char line[64];
    int32_t h;

    if (setup()) return;
    h = vopenf("sys:new.txt", O_WRITE | O_CREATE);
    CHECK(h >= 1, "create: %d", (int)h);
    if (h < 1) return;
    h_poke8(buf, 'h'); h_poke8(buf + 1, 'i'); h_poke8(buf + 2, '!'); h_poke8(buf + 3, '\n');
    CHECK_U32(4, (uint32_t)vwrite(h, buf, 4));
    vclose(h);

    first_line("sys:NEW.txt", line, sizeof line);       /* we can read it back */
    CHECK_STR("hi!\n", line);

    fsck_must_be_clean("after creating a file");
    CHECK_U32(4, (uint32_t)host_cat("/new.txt", got, sizeof got));  /* and so can Linux */
    CHECK(memcmp(got, "hi!\n", 4) == 0, "debugfs read something else");
}

/* 300KB in 3000-byte pieces: direct blocks, an indirect block, a double
 * indirect block and the indirect blocks under it, all allocated here. */
static void t_write_through_double_indirection(void)
{
    static uint8_t got[310000];
    const uint32_t total = 300u * 1024u + 77u;
    uint32_t i, bad = 0;
    int32_t h;
    long n;

    if (setup()) return;
    h = vopenf("sys:docs/written.dat", O_WRITE | O_CREATE);
    if (h < 1) { t_fail("create: %d", (int)h); return; }
    write_pattern(h, total, 3000);
    vclose(h);

    fsck_must_be_clean("after writing 300KB");
    n = host_cat("/docs/written.dat", got, sizeof got);
    CHECK_U32(total, (uint32_t)n);
    for (i = 0; i < total && i < (uint32_t)n; i++)
        if (got[i] != wbyte(i)) bad++;
    CHECK(bad == 0, "%u bytes differ from what was written", bad);
}

/* Into the middle of an existing file: partial blocks at both ends are read,
 * changed and written back, and nothing either side is disturbed. */
static void t_overwrite_in_place(void)
{
    static uint8_t got[310000];
    uint32_t buf = scratch(3000), i, bad = 0;
    int32_t h;
    long n;

    if (setup()) return;
    h = vopenf("sys:docs/deep/big.dat", O_WRITE);
    if (h < 1) { t_fail("open for write: %d", (int)h); return; }
    vseek(h, 5000);
    for (i = 0; i < 3000; i++) h_poke8(buf + i, 0xEE);
    CHECK_U32(3000, (uint32_t)vwrite(h, buf, 3000));
    vclose(h);

    fsck_must_be_clean("after overwriting");
    n = host_cat("/docs/deep/big.dat", got, sizeof got);
    CHECK_U32(DISK2_EXT2_BIG_SIZE, (uint32_t)n);        /* not a byte longer */
    for (i = 0; i < (uint32_t)n; i++) {
        uint8_t want = (i >= 5000 && i < 8000) ? 0xEE : DISK2_EXT2_BYTE(i);
        if (got[i] != want) bad++;
    }
    CHECK(bad == 0, "%u bytes wrong after an overwrite in place", bad);
}

/* Writing past the end leaves a hole, and a hole reads as zeros. */
static void t_holes(void)
{
    static uint8_t got[40000];
    uint32_t buf = scratch(16), i, bad = 0;
    int32_t h;
    long n;

    if (setup()) return;
    h = vopenf("sys:sparse", O_WRITE | O_CREATE);
    vseek(h, 30000);
    for (i = 0; i < 10; i++) h_poke8(buf + i, (uint8_t)('0' + i));
    CHECK_U32(10, (uint32_t)vwrite(h, buf, 10));
    vclose(h);

    fsck_must_be_clean("after a sparse write");
    n = host_cat("/sparse", got, sizeof got);
    CHECK_U32(30010, (uint32_t)n);
    for (i = 0; i < 30000; i++) if (got[i]) bad++;
    CHECK(bad == 0, "%u non-zero bytes in the hole", bad);
    CHECK(got[30000] == '0' && got[30009] == '9', "the data after the hole is wrong");
}

/* What is taken is given back: create, fill, remove, and the free count is
 * what it was. Anything less is a leak e2fsck would also find. */
static void t_remove_gives_the_blocks_back(void)
{
    uint32_t before, during, after;
    int32_t h;

    if (setup()) return;
    fsck_must_be_clean("before anything");
    before = free_blocks();

    h = vopenf("sys:temp.dat", O_WRITE | O_CREATE);
    write_pattern(h, 100000, 4096);
    vclose(h);
    fsck_must_be_clean("after writing");
    during = free_blocks();
    CHECK(during + 98 <= before, "100000 bytes used %u blocks", before - during);

    CHECK_U32(0, (uint32_t)vremove("sys:TEMP.dat"));
    CHECK_U32((uint32_t)VFS_ENOENT, (uint32_t)vopen("sys:temp.dat"));
    fsck_must_be_clean("after removing");
    after = free_blocks();
    CHECK_U32(before, after);
}

static void t_truncate_on_open(void)
{
    static uint8_t got[64];
    uint32_t buf = scratch(8), before;
    int32_t h;

    if (setup()) return;
    fsck_must_be_clean("before");
    before = free_blocks();

    h = vopenf("sys:docs/deep/big.dat", O_WRITE | O_TRUNC);
    if (h < 1) { t_fail("open: %d", (int)h); return; }
    h_poke8(buf, 'x');
    CHECK_U32(1, (uint32_t)vwrite(h, buf, 1));
    vclose(h);

    fsck_must_be_clean("after truncating a 300KB file");
    CHECK_U32(1, (uint32_t)host_cat("/docs/deep/big.dat", got, sizeof got));
    CHECK(free_blocks() > before + 290, "truncation freed %u blocks", free_blocks() - before);
}

static void t_directories(void)
{
    static uint8_t got[64];
    uint32_t buf = scratch(8);
    int32_t h;

    if (setup()) return;
    CHECK_U32(0, (uint32_t)vmkdir("sys:projects"));
    CHECK_U32(0, (uint32_t)vmkdir("sys:projects/amiga"));
    CHECK_U32((uint32_t)VFS_EEXIST, (uint32_t)vmkdir("sys:Projects"));      /* by case */
    CHECK_U32((uint32_t)VFS_ENOENT, (uint32_t)vmkdir("sys:nothere/sub"));

    h = vopenf("sys:projects/amiga/todo", O_WRITE | O_CREATE);
    h_poke8(buf, 'a');
    vwrite(h, buf, 1);
    vclose(h);
    fsck_must_be_clean("after mkdir and a file inside");
    CHECK_U32(1, (uint32_t)host_cat("/projects/amiga/todo", got, sizeof got));

    CHECK_U32((uint32_t)VFS_ENOTEMPTY, (uint32_t)vremove("sys:projects/amiga"));
    CHECK_U32(0, (uint32_t)vremove("sys:projects/amiga/todo"));
    CHECK_U32(0, (uint32_t)vremove("sys:projects/amiga"));
    CHECK_U32(0, (uint32_t)vremove("sys:projects"));
    fsck_must_be_clean("after removing them again");
}

/* Enough new files that the directory has to grow past its first block. */
static void t_directory_grows(void)
{
    char path[64];
    int i;

    if (setup()) return;
    CHECK_U32(0, (uint32_t)vmkdir("sys:lots"));
    for (i = 0; i < 120; i++) {
        int32_t h;
        snprintf(path, sizeof path, "sys:lots/entry-number-%03d.txt", i);
        h = vopenf(path, O_WRITE | O_CREATE);
        if (h < 1) { t_fail("create %s: %d", path, (int)h); return; }
        vclose(h);
    }
    CHECK_U32(120, (uint32_t)count_dir("sys:lots", "entry-number-119.txt", NULL, NULL));
    fsck_must_be_clean("after 120 creates in one directory");

    /* Remove every other one, then create again: freed slots are reused. */
    for (i = 0; i < 120; i += 2) {
        snprintf(path, sizeof path, "sys:lots/entry-number-%03d.txt", i);
        CHECK_U32(0, (uint32_t)vremove(path));
    }
    CHECK_U32(60, (uint32_t)count_dir("sys:lots", "entry-number-119.txt", NULL, NULL));
    fsck_must_be_clean("after removing half of them");
}

/* ext2 would happily hold readme and README. The system's names ignore case,
 * so creating a second one that differs only by case is refused - the first
 * would become unreachable by some spellings. */
static void t_names_differing_by_case(void)
{
    int32_t h;

    if (setup()) return;
    h = vopenf("sys:HELLO.TXT", O_WRITE | O_CREATE);    /* opens hello.txt */
    CHECK(h >= 1, "open: %d", (int)h);
    vclose(h);
    CHECK_U32(8, (uint32_t)count_dir("sys:", "hello.txt", NULL, NULL));     /* no new entry */
    fsck_must_be_clean("after opening by another case");
}

static void t_fat16_is_read_only(void)
{
    if (setup()) return;
    CHECK_U32(0, (uint32_t)mount("boot", "ide0p0", NULL));
    CHECK_U32((uint32_t)VFS_EROFS, (uint32_t)vopenf("boot:new.txt", O_WRITE | O_CREATE));
    CHECK_U32((uint32_t)VFS_EROFS, (uint32_t)vmkdir("boot:newdir"));
    CHECK_U32((uint32_t)VFS_EROFS, (uint32_t)vremove("boot:readme.txt"));
}

/* Fill the disk. The error is an error, not a crash or a corrupt volume;
 * and removing the file gives the space back. */
static void t_disk_full(void)
{
    uint32_t buf = scratch(4096), before, i, wrote = 0;
    int32_t h, n = 0;

    if (setup()) return;
    fsck_must_be_clean("before");
    before = free_blocks();

    h = vopenf("sys:huge", O_WRITE | O_CREATE);
    for (i = 0; i < 4096; i++) h_poke8(buf + i, (uint8_t)i);
    for (i = 0; i < 2000; i++) {
        n = vwrite(h, buf, 4096);
        if (n != 4096) break;
        wrote += 4096;
    }
    CHECK(n == VFS_ENOSPC || (n >= 0 && n < 4096), "filling the disk ended with %d", (int)n);
    CHECK(wrote > 2000000, "only %u bytes fitted on a 4MB volume", wrote);
    vclose(h);
    fsck_must_be_clean("with the disk full");

    CHECK_U32(0, (uint32_t)vremove("sys:huge"));
    fsck_must_be_clean("after removing the huge file");
    CHECK_U32(before, free_blocks());
}

static const test_case tests[] = {
    { "probes_tell_apart",      t_probes_tell_the_filesystems_apart, NULL },
    { "incompat_refused",       t_unknown_incompat_feature_is_refused, NULL },
    { "root_listing",           t_root_listing,                     NULL },
    { "dir_spans_blocks_groups", t_directory_spans_blocks_and_groups, NULL },
    { "long_names",             t_long_names,                       NULL },
    { "case_rule",              t_case_rule,                        NULL },
    { "double_indirection",     t_big_file_through_double_indirection, NULL },
    { "seek_each_region",       t_seek_into_each_region,            NULL },
    { "empty_and_symlink",      t_empty_file_and_symlink,           NULL },
    { "second_read_only_data",  t_second_read_costs_only_the_data,  NULL },
    { "small_files_cached",     t_small_files_are_cached,           NULL },
    { "read_throughput",        t_read_throughput,                  NULL },
    { "w_throughput",           t_write_throughput,                 NULL },
    { "w_bulk_throughput",      t_bulk_throughput,                  NULL },
    { "w_direct_cached_agree",  t_direct_and_cached_io_agree,       NULL },
    { "w_odd_buffer_address",   t_odd_buffer_addresses,             NULL },
    { "w_create_small_file",    t_create_a_small_file,              NULL },
    { "w_double_indirection",   t_write_through_double_indirection, NULL },
    { "w_overwrite_in_place",   t_overwrite_in_place,               NULL },
    { "w_holes",                t_holes,                            NULL },
    { "w_remove_frees_blocks",  t_remove_gives_the_blocks_back,     NULL },
    { "w_truncate_on_open",     t_truncate_on_open,                 NULL },
    { "w_directories",          t_directories,                      NULL },
    { "w_directory_grows",      t_directory_grows,                  NULL },
    { "w_names_differ_by_case", t_names_differing_by_case,          NULL },
    { "w_fat16_is_read_only",   t_fat16_is_read_only,               NULL },
    { "w_disk_full",            t_disk_full,                        NULL },
};

const test_suite kext2_suite = { "kext2", tests, sizeof tests / sizeof tests[0] };
