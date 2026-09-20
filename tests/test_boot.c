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
#define BI_CPU_TYPE     0x2C    /* short, version 2 */
#define BI_FPU_TYPE     0x2E    /* short, version 2 */
#define BI_V2_SIZE      0x30

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
    CHECK_U32(2, h_peek16(bi + BI_VERSION));
    CHECK_U32(BI_V2_SIZE, h_peek16(bi + BI_SIZE));
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
    CHECK_U32(0, h_peek16(bi + BI_CPU_TYPE));           /* CPU_68000 */
    CHECK_U32(0, h_peek16(bi + BI_FPU_TYPE));           /* FPU_NONE */
}

/* --- CPU detection ---------------------------------------------------------
 *
 * Each probe is an instruction the next CPU up added, and on the wrong CPU
 * it traps - through a frame whose shape is one of the things being
 * detected. So this is run on every core Musashi has. There is no 68060
 * core; that branch is reasoned, not tested.
 */
static void detect_on(int model, uint32_t want_cpu)
{
    uint32_t illegal, line_f, d0, sp;
    h_result r;

    h_reset();
    h_set_cpu(model);
    h_begin_call();
    r = h_call(h_sym("install_exception_vectors"));
    CHECK_CALL(r);
    illegal = h_peek32(0x10);
    line_f  = h_peek32(0x2C);

    h_begin_call();
    h_set_d(2, 0xD2D2D2D2u);
    h_set_a(5, 0x00A5A5A4u);
    sp = h_get_sp();
    r = h_call(h_sym("_rom_cpu_detect"));
    CHECK_CALL(r);
    d0 = h_get_d(0);

    CHECK(( d0 & 0xFFFF) == want_cpu,
          "on a %d: detected CPU type %u, want %u", model, d0 & 0xFFFF, want_cpu);
    if (model < 68020)
        CHECK_U32(0, d0 >> 16);                         /* no FPU interface */

    /* It borrowed two vectors and the stack pointer; all three go back. */
    CHECK_U32(illegal, h_peek32(0x10));
    CHECK_U32(line_f,  h_peek32(0x2C));
    CHECK_U32(sp, h_get_sp());
    CHECK_U32(0xD2D2D2D2u, h_get_d(2));
    CHECK_U32(0x00A5A5A4u, h_get_a(5));
}

static void t_cpu_detect_68000(void) { detect_on(68000, 0); }
static void t_cpu_detect_68010(void) { detect_on(68010, 1); }
static void t_cpu_detect_68020(void) { detect_on(68020, 2); }
static void t_cpu_detect_68030(void) { detect_on(68030, 3); }
static void t_cpu_detect_68040(void) { detect_on(68040, 4); }

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
    { "cpu_detect_68000",       t_cpu_detect_68000,                 NULL },
    { "cpu_detect_68010",       t_cpu_detect_68010,                 NULL },
    { "cpu_detect_68020",       t_cpu_detect_68020,                 NULL },
    { "cpu_detect_68030",       t_cpu_detect_68030,                 NULL },
    { "cpu_detect_68040",       t_cpu_detect_68040,                 NULL },
    { "kernel_reads_handoff",   t_kernel_reads_the_handoff,         NULL },
    { "kernel_rejects_bad",     t_kernel_rejects_a_bad_handoff,     NULL },
    { "kernel_shorter_struct",  t_kernel_tolerates_a_shorter_struct, NULL },
};

const test_suite boot_suite = { "boot", tests, sizeof tests / sizeof tests[0] };
