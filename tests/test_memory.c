/*
 * test_memory.c - memory detection and the map handed to the kernel
 *
 * These did not exist before the C conversion, because detect_fast_ram
 * sizes memory by probing one megabyte past the end and relying on the
 * read-back to fail. The harness treated that probe as a fault, so the
 * routine could not run here at all. It now models the Zorro II bus above
 * the fast RAM as floating, which is what real hardware does and what the
 * routine was always relying on.
 *
 * The model has 2MB of chip and 1MB of fast, so the expected values below
 * are derived from H_CHIP_SIZE and H_FAST_SIZE rather than written out.
 */
#include "protocol.h"

/* MemEntry: base (long), size (long), type (word), flags (word). */
#define ENTRY_SIZE      12
#define MEM_TYPE_CHIP     1
#define MEM_TYPE_FAST     2
#define MEM_TYPE_ROM      5
#define MEM_TYPE_RESERVED 6

#define KERNEL_CHIP     0x4000u
#define KERNEL_STACK    0x2000u         /* 8KB reserved at the top of fast */
#define MEMMAP_MAX_SCAN 36

static uint32_t entry(int n)
{
    return h_sym("MEMMAP_TABLE") + (uint32_t)n * ENTRY_SIZE;
}

static void check_entry(int n, uint32_t base, uint32_t size, uint32_t type)
{
    uint32_t e = entry(n);
    CHECK_U32(base, h_peek32(e + 0));
    CHECK_U32(size, h_peek32(e + 4));
    CHECK_U32(type, h_peek16(e + 8));
}

/* --- detection ---------------------------------------------------------- */

static void t_detect_chip_ram(void)
{
    h_result r;
    h_begin_call();
    r = h_call(h_sym("detect_chip_ram"));
    CHECK_CALL(r);
    CHECK_U32(H_CHIP_SIZE, h_get_d(0));
}

static void t_detect_fast_ram(void)
{
    /* Stops where the Zorro bus starts floating, one megabyte up. */
    h_result r;
    h_begin_call();
    r = h_call(h_sym("detect_fast_ram"));
    CHECK_CALL(r);
    CHECK_U32(H_FAST_SIZE, h_get_d(0));
}

/* --- the table ---------------------------------------------------------- */

static void build(void)
{
    h_result r;
    h_begin_call();
    r = h_call(h_sym("build_memory_table"));
    CHECK_CALL(r);
}

static void t_table_reserved_low(void)
{
    build();
    check_entry(0, 0, KERNEL_CHIP, MEM_TYPE_RESERVED);
}

static void t_table_chip(void)
{
    build();
    check_entry(1, KERNEL_CHIP, H_CHIP_SIZE - KERNEL_CHIP, MEM_TYPE_CHIP);
}

static void t_table_fast(void)
{
    build();
    check_entry(2, H_FAST_BASE, H_FAST_SIZE - KERNEL_STACK, MEM_TYPE_FAST);
}

static void t_table_kernel_stack(void)
{
    build();
    check_entry(3, H_FAST_BASE + H_FAST_SIZE - KERNEL_STACK,
                KERNEL_STACK, MEM_TYPE_RESERVED);
}

static void t_table_rom(void)
{
    build();
    check_entry(4, H_ROM_BASE, H_ROM_SIZE, MEM_TYPE_ROM);
}

static void t_table_terminated(void)
{
    build();
    check_entry(5, 0, 0, 0);
}

static void t_table_regions_do_not_overlap(void)
{
    int n;

    build();

    for (n = 0; n < 5 && !t_failed(); n++) {
        uint32_t base = h_peek32(entry(n) + 0);
        uint32_t size = h_peek32(entry(n) + 4);
        uint32_t next = h_peek32(entry(n + 1) + 0);
        uint32_t nsize = h_peek32(entry(n + 1) + 4);

        if (nsize == 0)
            break;
        CHECK(base + size <= next,
              "entry %d ($%X+$%X) runs into entry %d ($%X)",
              n, base, size, n + 1, next);
    }
}

/* --- reserving the kernel image ------------------------------------------
 *
 * The table is built before SYSTEM.BIN is read, so its fast RAM entry starts
 * at $200000 and covers whatever gets loaded there. Until this ran, the
 * kernel was handed its own image as free memory and mem.c had to clamp to
 * its _end symbol to avoid allocating over itself.
 */

#define KERNEL_LOAD_ADDR H_FAST_BASE

static void reserve(uint32_t bytes)
{
    h_result r;
    h_begin_call();
    h_set_d(0, bytes);
    r = h_call(h_sym("reserve_kernel_image"));
    CHECK_CALL(r);
    CHECK_U32(0u, h_get_d(0));
}

static void t_reserve_splits_fast(void)
{
    /* 5000 bytes rounds up to 8192. */
    build();
    reserve(5000);

    check_entry(2, KERNEL_LOAD_ADDR, 0x2000u, MEM_TYPE_RESERVED);
    check_entry(3, KERNEL_LOAD_ADDR + 0x2000u,
                H_FAST_SIZE - KERNEL_STACK - 0x2000u, MEM_TYPE_FAST);
}

static void t_reserve_keeps_stack_and_rom(void)
{
    /* The entries after the split must survive the shift intact. */
    build();
    reserve(5000);

    check_entry(4, H_FAST_BASE + H_FAST_SIZE - KERNEL_STACK,
                KERNEL_STACK, MEM_TYPE_RESERVED);
    check_entry(5, H_ROM_BASE, H_ROM_SIZE, MEM_TYPE_ROM);
    check_entry(6, 0, 0, 0);
}

static void t_reserve_rounds_up(void)
{
    build();
    reserve(1);
    check_entry(2, KERNEL_LOAD_ADDR, 0x1000u, MEM_TYPE_RESERVED);
}

static void t_reserve_no_free_fast_overlaps_kernel(void)
{
    /* The property that actually matters: after reserving, no entry marked
     * FAST contains the kernel load address. */
    int n;

    build();
    reserve(5000);

    for (n = 0; n < MEMMAP_MAX_SCAN && !t_failed(); n++) {
        uint32_t base = h_peek32(entry(n) + 0);
        uint32_t size = h_peek32(entry(n) + 4);
        uint32_t type = h_peek16(entry(n) + 8);

        if (size == 0)
            break;
        if (type != MEM_TYPE_FAST)
            continue;
        CHECK(KERNEL_LOAD_ADDR < base || KERNEL_LOAD_ADDR >= base + size,
              "free fast entry %d ($%X+$%X) still contains the kernel image",
              n, base, size);
    }
}

/* ------------------------------------------------------------------------ */

static const test_case tests[] = {
    { "detect_chip_ram",   t_detect_chip_ram,   NULL },
    { "detect_fast_ram",   t_detect_fast_ram,   NULL },
    { "table_reserved_low",t_table_reserved_low,NULL },
    { "table_chip",        t_table_chip,        NULL },
    { "table_fast",        t_table_fast,        NULL },
    { "table_kernel_stack",t_table_kernel_stack,NULL },
    { "table_rom",         t_table_rom,         NULL },
    { "table_terminated",  t_table_terminated,  NULL },
    { "table_no_overlap",  t_table_regions_do_not_overlap, NULL },
    { "reserve_splits_fast",     t_reserve_splits_fast,     NULL },
    { "reserve_keeps_tail",      t_reserve_keeps_stack_and_rom, NULL },
    { "reserve_rounds_up",       t_reserve_rounds_up,       NULL },
    { "reserve_frees_no_kernel", t_reserve_no_free_fast_overlaps_kernel, NULL },
};

const test_suite memory_suite = { "mem", tests, sizeof tests / sizeof tests[0] };
