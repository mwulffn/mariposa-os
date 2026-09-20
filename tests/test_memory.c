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
#define MEM_TYPE_SLOW     3
#define MEM_TYPE_ROM      5
#define MEM_TYPE_RESERVED 6

/*
 * Written out rather than included from src/shared/memmap.h on purpose: the
 * bit numbers are the thing under test. The kernel's mem.h had them the
 * other way round, and nothing here noticed.
 */
#define MEMF_DMA          (1u << 0)
#define MEMF_TESTED       (1u << 1)

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

static void check_flags(int n, uint32_t flags)
{
    CHECK_U32(flags, h_peek16(entry(n) + 10));
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

/*
 * MEMF_DMA says the chipset can reach the region, so only chip RAM may
 * carry it. Zorro II fast RAM is off the chip bus - a DMA buffer allocated
 * there would transfer garbage - and it is only probed, never walked, so it
 * is not MEMF_TESTED either.
 */
static void t_table_flags(void)
{
    build();
    check_flags(0, MEMF_DMA);                     /* low chip, not walked */
    check_flags(1, MEMF_DMA | MEMF_TESTED);       /* chip, walked */
    check_flags(2, 0);                            /* fast: no DMA */
    check_flags(3, 0);                            /* kernel stack, in fast */
    check_flags(4, 0);                            /* ROM */
}

static void t_no_fast_region_claims_dma(void)
{
    int n;

    build();

    for (n = 0; n < MEMMAP_MAX_SCAN && !t_failed(); n++) {
        uint32_t base  = h_peek32(entry(n) + 0);
        uint32_t size  = h_peek32(entry(n) + 4);
        uint32_t flags = h_peek16(entry(n) + 10);

        if (size == 0)
            break;
        if (base < H_FAST_BASE)
            continue;
        CHECK(!(flags & MEMF_DMA),
              "entry %d ($%X+$%X) is above chip RAM but claims MEMF_DMA",
              n, base, size);
    }
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

/* --- the kernel stack ----------------------------------------------------
 *
 * bootstrap.s hands the kernel this value in A7. It used to find it by
 * taking the first RESERVED entry at or above $200000, which silently
 * became the loaded kernel image once reserve_kernel_image started carving
 * that out - so the kernel ran with its stack on top of its own code, a few
 * hundred bytes below the round-up, and corrupted itself as soon as it grew.
 */
static uint32_t stack_top(void)
{
    h_result r;
    h_begin_call();
    r = h_call(h_sym("kernel_stack_top"));
    CHECK_CALL(r);
    return h_get_d(0);
}

static void t_stack_top_is_top_of_fast(void)
{
    build();
    CHECK_U32(H_FAST_BASE + H_FAST_SIZE, stack_top());
}

static void t_stack_top_survives_kernel_reservation(void)
{
    /* The regression: a second RESERVED entry appears at $200000. */
    build();
    reserve(5000);
    CHECK_U32(H_FAST_BASE + H_FAST_SIZE, stack_top());
}

static void t_stack_top_is_not_in_the_kernel_image(void)
{
    /* The property that actually bit: the stack must not sit inside the
     * image the ROM just reserved for the kernel. */
    uint32_t top;

    build();
    reserve(5000);
    top = stack_top();

    CHECK(top > KERNEL_LOAD_ADDR + 0x2000u,
          "stack top $%X is inside the reserved kernel image "
          "($%X+$2000) - the kernel would overwrite itself",
          top, KERNEL_LOAD_ADDR);
}

/* --- slow / trapdoor RAM -------------------------------------------------
 *
 * The $C00000 expansion. Two hazards, both measured on FS-UAE before this
 * was written: unpopulated address space there floats to changing chip-bus
 * data rather than to all-ones, and a partially decoded board mirrors itself
 * up the region. detect_slow_ram writes and reads back for the first, and
 * uses the two-pattern distinctness test for the second.
 */
#define SLOW_BASE   H_SLOW_BASE
#define SLOW_512K   0x080000u
#define SLOW_1536K  0x180000u

static uint32_t detect_slow(void)
{
    h_result r;
    h_begin_call();
    r = h_call(h_sym("detect_slow_ram"));
    CHECK_CALL(r);
    return h_get_d(0);
}

static void t_slow_none(void)
{
    CHECK_U32(0u, detect_slow());
}

static void t_slow_512k(void)
{
    h_attach_slow_ram(SLOW_512K);
    CHECK_U32(SLOW_512K, detect_slow());
}

static void t_slow_1mb(void)
{
    h_attach_slow_ram(0x100000u);
    CHECK_U32(0x100000u, detect_slow());
}

/*
 * A fully decoded 1.5MB board fills Gary's whole window. Sizing one step
 * past it would touch $D80000, which is Gayle - unmapped here, so the
 * harness would fault the call rather than let it pass quietly.
 */
static void t_slow_stops_below_gayle(void)
{
    h_attach_slow_ram(SLOW_1536K);
    CHECK_U32(SLOW_1536K, detect_slow());
}

/*
 * 512KB of real memory answering across the whole 1.5MB window. A write and
 * read-back alone measures 1.5MB and is wrong by a megabyte.
 */
static void t_slow_mirrored_board_sizes_real_memory(void)
{
    h_attach_slow_ram_mirrored(SLOW_512K, SLOW_1536K);
    CHECK_U32(SLOW_512K, detect_slow());
}

static void t_slow_absent_leaves_table_alone(void)
{
    build();
    check_entry(4, H_ROM_BASE, H_ROM_SIZE, MEM_TYPE_ROM);
    check_entry(5, 0, 0, 0);
}

static void t_slow_in_table(void)
{
    h_attach_slow_ram(SLOW_512K);
    build();
    check_entry(4, SLOW_BASE, SLOW_512K, MEM_TYPE_SLOW);
    check_entry(5, H_ROM_BASE, H_ROM_SIZE, MEM_TYPE_ROM);
    check_entry(6, 0, 0, 0);
}

/* On the chip bus, but Agnus cannot DMA to it - so no flags, like fast. */
static void t_slow_is_not_dma(void)
{
    h_attach_slow_ram(SLOW_512K);
    build();
    check_flags(4, 0);
}

/* Slow RAM sits above fast RAM, so a stack search that only asked for
 * "reserved and above $200000" could wander into it. */
static void t_slow_does_not_confuse_stack_search(void)
{
    h_attach_slow_ram(SLOW_512K);
    build();
    reserve(5000);
    CHECK_U32(H_FAST_BASE + H_FAST_SIZE, stack_top());
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
    { "table_flags",       t_table_flags,       NULL },
    { "flags_no_fast_dma", t_no_fast_region_claims_dma, NULL },
    { "reserve_splits_fast",     t_reserve_splits_fast,     NULL },
    { "reserve_keeps_tail",      t_reserve_keeps_stack_and_rom, NULL },
    { "reserve_rounds_up",       t_reserve_rounds_up,       NULL },
    { "reserve_frees_no_kernel", t_reserve_no_free_fast_overlaps_kernel, NULL },
    { "stack_top",               t_stack_top_is_top_of_fast, NULL },
    { "stack_top_after_reserve", t_stack_top_survives_kernel_reservation, NULL },
    { "stack_not_in_kernel",     t_stack_top_is_not_in_the_kernel_image, NULL },
    { "slow_none",               t_slow_none,          NULL },
    { "slow_512k",               t_slow_512k,          NULL },
    { "slow_1mb",                t_slow_1mb,           NULL },
    { "slow_stops_below_gayle",  t_slow_stops_below_gayle, NULL },
    { "slow_mirrored",           t_slow_mirrored_board_sizes_real_memory, NULL },
    { "slow_absent_table",       t_slow_absent_leaves_table_alone, NULL },
    { "slow_in_table",           t_slow_in_table,      NULL },
    { "slow_not_dma",            t_slow_is_not_dma,    NULL },
    { "slow_stack_search",       t_slow_does_not_confuse_stack_search, NULL },
};

const test_suite memory_suite = { "mem", tests, sizeof tests / sizeof tests[0] };
