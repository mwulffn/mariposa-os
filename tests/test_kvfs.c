/*
 * test_kvfs.c - vfs.c and fs_fat16.c: volumes, paths, handles, FAT16
 *
 * Against the FAT16 volume on two-part.img: README.TXT and SYSTEM.BIN in the
 * root beside a volume label and a deleted entry, DOCS/ with a VFAT long
 * name entry and NOTES.TXT, and DOCS/DEEP/FILE.DAT - three clusters,
 * deliberately scattered.
 */
#include "protocol.h"
#include "disk_layout.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define VFS_ENOENT   (-1)
#define VFS_EBADF    (-3)
#define VFS_ENOTDIR  (-4)
#define VFS_EISDIR   (-5)
#define VFS_ENODEV   (-7)
#define VFS_EBUSY    (-8)

#define CHIP_BASE  0x140000u
#define FAST_BASE   h_kernel_heap(0x40000u)

static const char *g_dir = "build";
void t_kvfs_set_disk_dir(const char *dir) { g_dir = dir; }

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

static int32_t mount(const char *vol, const char *dev, const char *type)
{
    uint32_t a[3]; a[0] = h_str(vol); a[1] = h_str(dev); a[2] = type ? h_str(type) : 0;
    return (int32_t)kcall("kernel:_vfs_mount", 3, a);
}

static int setup(void)
{
    uint8_t map[24];
    uint32_t a[2];
    char path[512];

    snprintf(path, sizeof path, "%s/%s", g_dir, DISK2_IMAGE);
    if (h_attach_disk(path)) { t_fail("no disk image"); return -1; }

    memset(map, 0, sizeof map);
    map[1] = (uint8_t)(FAST_BASE >> 16); map[5] = 0x04; map[9] = 2;   /* 256KB fast */
    a[0] = h_alloc(map, sizeof map); a[1] = 0;
    kcall("kernel:_mem_init", 2, a);
    kcall("kernel:_ser_init", 0, NULL);
    kcall("kernel:_blk_init", 0, NULL);
    kcall("kernel:_bc_init", 0, NULL);
    kcall("kernel:_vfs_init", 0, NULL);
    a[0] = h_sym("kernel:_fat16_fs");
    kcall("kernel:_vfs_register_fs", 1, a);
    CHECK_U32(0, (uint32_t)mount("boot", "ide0p0", NULL));
    return 0;
}

static int32_t vopen(const char *path)    { uint32_t a = h_str(path); return (int32_t)kcall("kernel:_vfs_open", 1, &a); }
static int32_t vopendir(const char *path) { uint32_t a = h_str(path); return (int32_t)kcall("kernel:_vfs_opendir", 1, &a); }
static int32_t vclose(int32_t h)          { uint32_t a = (uint32_t)h; return (int32_t)kcall("kernel:_vfs_close", 1, &a); }

static int32_t vread(int32_t h, uint32_t buf, uint32_t len)
{
    uint32_t a[3]; a[0] = (uint32_t)h; a[1] = buf; a[2] = len;
    return (int32_t)kcall("kernel:_vfs_read", 3, a);
}

static uint32_t scratch(uint32_t n)
{
    static uint8_t zero[4096];
    return h_alloc(zero, n);
}

/* --- mounting -------------------------------------------------------------- */

static void t_mount_probes_the_filesystem(void)
{
    if (setup()) return;
    /* ide0p1 holds no filesystem; nothing may claim it. */
    CHECK_U32((uint32_t)VFS_ENODEV, (uint32_t)mount("junk", "ide0p1", NULL));
    /* Not even when told to: a named type is believed only if its probe agrees. */
    CHECK_U32((uint32_t)VFS_ENODEV, (uint32_t)mount("junk", "ide0p1", "fat16"));
    CHECK_U32((uint32_t)VFS_ENODEV, (uint32_t)mount("x", "nosuchdev", NULL));
    CHECK_U32((uint32_t)VFS_EBUSY,  (uint32_t)mount("BOOT", "ide0p0", NULL));  /* name taken */
}

/* --- directories ----------------------------------------------------------- */

/* struct vfs_dirent: name[256], type, size */
static int list(const char *path, char names[][16], uint32_t *types, int max)
{
    uint32_t ent = scratch(264), a[2];
    int32_t h = vopendir(path);
    int n = 0;

    if (h < 1) { t_fail("opendir %s: %d", path, (int)h); return -1; }
    a[0] = (uint32_t)h; a[1] = ent;
    while (n < max && (int32_t)kcall("kernel:_vfs_readdir", 2, a) == 1) {
        h_peekstr(ent, names[n], 16);
        types[n] = h_peek32(ent + 256);
        n++;
    }
    vclose(h);
    return n;
}

static int has(char names[][16], int n, const char *want)
{
    int i;
    for (i = 0; i < n; i++)
        if (strcmp(names[i], want) == 0) return i;
    return -1;
}

static void t_root_lists_what_is_there(void)
{
    char names[16][16];
    uint32_t types[16];
    int n;

    if (setup()) return;
    n = list("boot:", names, types, 16);
    CHECK_U32(3, (uint32_t)n);          /* not the label, not the deleted file */
    CHECK(has(names, n, "README.TXT") >= 0, "no README.TXT");
    CHECK(has(names, n, "SYSTEM.BIN") >= 0, "no SYSTEM.BIN");
    CHECK(has(names, n, "DOCS") >= 0 && types[has(names, n, "DOCS")] == 2, "DOCS is not a directory");
}

static void t_subdirectory_lists(void)
{
    char names[16][16];
    uint32_t types[16];
    int n;

    if (setup()) return;
    n = list("boot:docs", names, types, 16);
    CHECK_U32(2, (uint32_t)n);          /* no ".", no "..", no long-name entry */
    CHECK(has(names, n, "NOTES.TXT") >= 0, "no NOTES.TXT");
    CHECK(has(names, n, "DEEP") >= 0, "no DEEP");
}

/* --- files ----------------------------------------------------------------- */

static void t_paths_ignore_case(void)
{
    uint32_t buf = scratch(64);
    char got[64];
    int32_t h;

    if (setup()) return;
    h = vopen("BOOT:Docs/notes.TXT");
    CHECK(h >= 1, "open: %d", (int)h);
    if (h < 1) return;
    CHECK_U32(strlen(DISK2_NOTES_TEXT), (uint32_t)vread(h, buf, 60));
    h_peekstr(buf, got, sizeof got);
    CHECK_STR(DISK2_NOTES_TEXT, got);
    CHECK_U32(0, (uint32_t)vread(h, buf, 60));          /* at the end */
    vclose(h);
}

/* Three clusters, scattered, read in pieces that straddle every boundary. */
static void t_scattered_file_reads_whole(void)
{
    uint32_t buf = scratch(128), total = 0, i;
    int32_t h, n;

    if (setup()) return;
    h = vopen("boot:docs/deep/file.dat");
    if (h < 1) { t_fail("open: %d", (int)h); return; }

    while ((n = vread(h, buf, 100)) > 0) {
        for (i = 0; i < (uint32_t)n; i++)
            if (h_peek8(buf + i) != DISK2_DATA_BYTE(total + i)) {
                t_fail("byte %u is wrong", total + i);
                vclose(h);
                return;
            }
        total += (uint32_t)n;
    }
    CHECK_U32(0, (uint32_t)n);
    CHECK_U32(DISK2_DATA_SIZE, total);
    vclose(h);
}

static void t_seek(void)
{
    uint32_t buf = scratch(16), a[2];
    int32_t h;

    if (setup()) return;
    h = vopen("boot:docs/deep/file.dat");
    if (h < 1) { t_fail("open: %d", (int)h); return; }

    a[0] = (uint32_t)h; a[1] = 1000;                    /* into the second cluster */
    kcall("kernel:_vfs_seek", 2, a);
    CHECK_U32(4, (uint32_t)vread(h, buf, 4));
    CHECK_U32(DISK2_DATA_BYTE(1000), h_peek8(buf));

    a[1] = 3;                                           /* and back */
    kcall("kernel:_vfs_seek", 2, a);
    CHECK_U32(4, (uint32_t)vread(h, buf, 4));
    CHECK_U32(DISK2_DATA_BYTE(3), h_peek8(buf));
    CHECK_U32(DISK2_DATA_BYTE(6), h_peek8(buf + 3));

    a[1] = DISK2_DATA_SIZE + 500;                       /* past the end: not an error */
    kcall("kernel:_vfs_seek", 2, a);
    CHECK_U32(0, (uint32_t)vread(h, buf, 4));
    vclose(h);
}

static void t_stat(void)
{
    uint32_t st = scratch(8), a[2];

    if (setup()) return;
    a[0] = h_str("boot:system.bin"); a[1] = st;
    CHECK_U32(0, kcall("kernel:_vfs_stat", 2, a));
    CHECK_U32(1, h_peek32(st));
    CHECK_U32(DISK_SYSBIN_SIZE, h_peek32(st + 4));
    a[0] = h_str("boot:docs");
    CHECK_U32(0, kcall("kernel:_vfs_stat", 2, a));
    CHECK_U32(2, h_peek32(st));
}

/* --- errors ---------------------------------------------------------------- */

static void t_errors_are_errors(void)
{
    uint32_t buf = scratch(16);
    int32_t h;

    if (setup()) return;
    CHECK_U32((uint32_t)VFS_ENOENT,  (uint32_t)vopen("boot:nothere.txt"));
    CHECK_U32((uint32_t)VFS_ENOENT,  (uint32_t)vopen("nosuchvolume:readme.txt"));
    CHECK_U32((uint32_t)VFS_ENOTDIR, (uint32_t)vopen("boot:readme.txt/inside"));
    CHECK_U32((uint32_t)VFS_EISDIR,  (uint32_t)vopen("boot:docs"));
    CHECK_U32((uint32_t)VFS_ENOTDIR, (uint32_t)vopendir("boot:readme.txt"));

    CHECK_U32((uint32_t)VFS_EBADF, (uint32_t)vread(0, buf, 4));
    CHECK_U32((uint32_t)VFS_EBADF, (uint32_t)vread(999, buf, 4));
    CHECK_U32((uint32_t)VFS_EBADF, (uint32_t)vread(-1, buf, 4));

    h = vopen("boot:readme.txt");
    CHECK_U32(0, (uint32_t)vclose(h));
    CHECK_U32((uint32_t)VFS_EBADF, (uint32_t)vclose(h));         /* twice */
    CHECK_U32((uint32_t)VFS_EBADF, (uint32_t)vread(h, buf, 4));  /* after close */

    h = vopendir("boot:docs");
    CHECK_U32((uint32_t)VFS_EBADF, (uint32_t)vread(h, buf, 4));  /* a directory is not a file */
    vclose(h);
}

static void t_handles_run_out_and_come_back(void)
{
    int32_t h[40];
    int i, got = 0;

    if (setup()) return;
    for (i = 0; i < 40; i++) {
        h[i] = vopen("boot:readme.txt");
        if (h[i] >= 1) got++;
    }
    CHECK_U32(32, (uint32_t)got);
    CHECK(h[39] < 0, "a 33rd handle");
    for (i = 0; i < 40; i++)
        if (h[i] >= 1) vclose(h[i]);
    CHECK_U32(0, kcall("kernel:_vfs_handles_in_use", 0, NULL));
    CHECK(vopen("boot:readme.txt") >= 1, "handles did not come back");
}

static void t_unmount_waits_for_open_files(void)
{
    uint32_t vol = h_str("boot");
    int32_t h;

    if (setup()) return;
    h = vopen("boot:readme.txt");
    CHECK_U32((uint32_t)VFS_EBUSY, kcall("kernel:_vfs_unmount", 1, &vol));
    vclose(h);
    CHECK_U32(0, kcall("kernel:_vfs_unmount", 1, &vol));
    CHECK_U32((uint32_t)VFS_ENOENT, (uint32_t)vopen("boot:readme.txt"));
    CHECK_U32(0, (uint32_t)mount("boot", "ide0p0", "FAT16"));    /* and again */
    CHECK(vopen("boot:readme.txt") >= 1, "remount did not work");
}

/* --- the cache, seen from up here ------------------------------------------ */

/* The filesystem driver was written for the ROM and knows nothing of any
 * cache, and gets one anyway: the second time, the directories and the FAT
 * cost nothing. The data itself is not cached - see blkdev.h - so it is
 * fetched again, and only it. */
static void t_second_read_of_a_file_is_free(void)
{
    uint32_t buf = scratch(2048);
    unsigned first, again;
    int32_t h;

    if (setup()) return;
    first = h_disk_sectors_read();
    h = vopen("boot:docs/deep/file.dat");
    vread(h, buf, 2000);
    vclose(h);
    first = h_disk_sectors_read() - first;

    again = h_disk_sectors_read();
    h = vopen("boot:docs/deep/file.dat");
    CHECK_U32(DISK2_DATA_SIZE, (uint32_t)vread(h, buf, 2000));
    vclose(h);
    again = h_disk_sectors_read() - again;

    CHECK(first > 5, "first read touched %u sectors", first);
    CHECK(again <= 3, "second read touched %u sectors: three hold the file", again);
}

static const test_case tests[] = {
    { "mount_probes",           t_mount_probes_the_filesystem,      NULL },
    { "root_listing",           t_root_lists_what_is_there,         NULL },
    { "subdirectory_listing",   t_subdirectory_lists,               NULL },
    { "paths_ignore_case",      t_paths_ignore_case,                NULL },
    { "scattered_file",         t_scattered_file_reads_whole,       NULL },
    { "seek",                   t_seek,                             NULL },
    { "stat",                   t_stat,                             NULL },
    { "errors",                 t_errors_are_errors,                NULL },
    { "handles_run_out",        t_handles_run_out_and_come_back,    NULL },
    { "unmount_waits",          t_unmount_waits_for_open_files,     NULL },
    { "second_read_is_free",    t_second_read_of_a_file_is_free,    NULL },
};

const test_suite kvfs_suite = { "kvfs", tests, sizeof tests / sizeof tests[0] };
