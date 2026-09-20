/*
 * test_boot.c - the ROM -> kernel handoff
 *
 * Both halves: the struct the ROM builds (src/shared/bootinfo.h), and the
 * real kernel image being entered with it. The offsets are spelled out here
 * instead of being taken from the header on purpose - they are an ABI
 * between two separately built binaries, and a test that included the header
 * would follow a reordered field instead of catching it.
 */
#include "protocol.h"

#include <stdint.h>
#include <string.h>

#define BOOTINFO_ADDR   0x3500u
#define BI_MAGIC        0x00    /* long  'BOOT' */
#define BI_VERSION      0x04    /* short */
#define BI_SIZE         0x06    /* short */
#define BI_MEMMAP       0x08
#define BI_ROM_PANIC    0x0C
#define BI_KERNEL_BASE  0x10
#define BI_KERNEL_SIZE  0x14
#define BI_STACK_TOP    0x18
#define BI_DEV_TYPE     0x1C    /* short */
#define BI_DEV_UNIT     0x1E    /* short */
#define BI_PART_LBA     0x20
#define BI_PART_BLOCKS  0x24
#define BI_ROM_VERSION  0x28    /* short */
#define BI_V1_SIZE      0x2C

#define KERNEL_BASE     0x200000u

static void build_table(void)
{
    h_result r;
    h_begin_call();
    r = h_call(h_sym("build_memory_table"));
    CHECK_CALL(r);
}

/* rom_build_bootinfo(part_lba, part_blocks, kernel_size, stack_top, panic) */
static uint32_t build_bootinfo(uint32_t lba, uint32_t blocks, uint32_t ksize,
                               uint32_t stack, uint32_t panic)
{
    h_result r;
    h_begin_call();
    h_push32(panic);
    h_push32(stack);
    h_push32(ksize);
    h_push32(blocks);
    h_push32(lba);
    r = h_call(h_sym("_rom_build_bootinfo"));
    CHECK_CALL(r);
    return h_get_d(0);
}

static void t_bootinfo_layout(void)
{
    uint32_t bi;

    build_table();
    bi = build_bootinfo(0x1234, 0x56789, 6000, 0x2F0000, 0xFC1000);

    CHECK_U32(BOOTINFO_ADDR, bi);
    CHECK_U32(0x424F4F54u, h_peek32(bi + BI_MAGIC));
    CHECK_U32(1, h_peek16(bi + BI_VERSION));
    CHECK_U32(BI_V1_SIZE, h_peek16(bi + BI_SIZE));
    CHECK_U32(h_sym("MEMMAP_TABLE"), h_peek32(bi + BI_MEMMAP));
    CHECK_U32(0xFC1000, h_peek32(bi + BI_ROM_PANIC));
    CHECK_U32(KERNEL_BASE, h_peek32(bi + BI_KERNEL_BASE));
    CHECK_U32(6000, h_peek32(bi + BI_KERNEL_SIZE));
    CHECK_U32(0x2F0000, h_peek32(bi + BI_STACK_TOP));
    CHECK_U32(1, h_peek16(bi + BI_DEV_TYPE));           /* BOOTDEV_IDE */
    CHECK_U32(0, h_peek16(bi + BI_DEV_UNIT));
    CHECK_U32(0x1234, h_peek32(bi + BI_PART_LBA));
    CHECK_U32(0x56789, h_peek32(bi + BI_PART_BLOCKS));
    CHECK_U32(h_peek16(H_ROM_BASE + 12), h_peek16(bi + BI_ROM_VERSION));
}

/* It lives in the low 16KB the map already marks reserved, and clear of its
 * neighbours: the memory map below it, the kernel's chip arena above. */
static void t_bootinfo_is_out_of_the_way(void)
{
    uint32_t sprintf_end = h_sym("SPRINTF_BUFFER") + 256;

    CHECK(BOOTINFO_ADDR >= sprintf_end, "overlaps the sprintf buffer");
    CHECK(BOOTINFO_ADDR + BI_V1_SIZE <= h_sym("KERNEL_CHIP"),
          "runs into kernel-managed chip RAM");
}

/* --- entering the kernel --------------------------------------------------
 *
 * The real SYSTEM.BIN, entered at _start the way bootstrap.s does it. There
 * is no vertical blank in the harness, so the kernel gets as far as waiting
 * for one and stops there - far enough to have read everything it was
 * handed and said so, all of it while serial is still polled.
 */
static const char *enter_kernel(uint32_t a0)
{
    h_result r;

    h_set_sp(0x2F0000);
    h_set_a(0, a0);
    h_set_a(1, h_sym("debugger_entry"));
    h_set_sr(0x2700);
    h_set_cycle_budget(60000000);
    r = h_run(h_sym("kernel:_start"));
    (void)r;                        /* never returns: H_TIMEOUT by design */
    return h_serial();
}

static void t_kernel_reads_the_handoff(void)
{
    const char *out;
    h_result r;

    h_begin_call();
    r = h_call(h_sym("install_exception_vectors"));
    CHECK_CALL(r);
    build_table();

    h_begin_call();
    h_set_d(0, 6000);
    r = h_call(h_sym("reserve_kernel_image"));
    CHECK_CALL(r);

    out = enter_kernel(build_bootinfo(0x800, 0x20000, 6000, 0x2F0000,
                                      h_sym("debugger_entry")));

    CHECK_CONTAINS("Kernel starting", out);
    CHECK_CONTAINS("Boot device: IDE unit 0, partition at block 2048, "
                   "131072 blocks", out);
    CHECK_CONTAINS("Memory system initialized", out);
    CHECK(strstr(out, "Debugger") == NULL, "kernel ended up in the debugger");
}

/* A ROM that predates the struct passes the memory map in A0. The kernel
 * must notice and say so, not walk a memory map as if it were a bootinfo. */
static void t_kernel_rejects_a_bad_handoff(void)
{
    const char *out;

    build_table();
    out = enter_kernel(h_sym("MEMMAP_TABLE"));

    CHECK_CONTAINS("bad boot info", out);
    CHECK_CONTAINS("AMAG Debugger", out);
    CHECK(strstr(out, "Memory system initialized") == NULL,
          "kernel carried on with a handoff it could not read");
}

/* A newer kernel on an older ROM: the struct is valid but shorter than the
 * kernel's idea of it. Fields past `size` are absent, not garbage. */
static void t_kernel_tolerates_a_shorter_struct(void)
{
    const char *out;
    uint32_t bi;

    build_table();
    bi = build_bootinfo(0x800, 0x20000, 6000, 0x2F0000,
                        h_sym("debugger_entry"));
    h_poke16(bi + BI_SIZE, BI_DEV_TYPE);        /* ends before the device */

    out = enter_kernel(bi);

    CHECK_CONTAINS("Boot device: not reported", out);
    CHECK_CONTAINS("Memory system initialized", out);
}

static const test_case tests[] = {
    { "bootinfo_layout",        t_bootinfo_layout,                  NULL },
    { "bootinfo_placement",     t_bootinfo_is_out_of_the_way,       NULL },
    { "kernel_reads_handoff",   t_kernel_reads_the_handoff,         NULL },
    { "kernel_rejects_bad",     t_kernel_rejects_a_bad_handoff,     NULL },
    { "kernel_shorter_struct",  t_kernel_tolerates_a_shorter_struct, NULL },
};

const test_suite boot_suite = { "boot", tests, sizeof tests / sizeof tests[0] };
