/*
 * test_kmem.c - src/kernel/mem.c, the free-list allocator
 *
 * Run out of the real SYSTEM.BIN, like kser.*. Each test hands mem_init a
 * memory map it built itself, so pool shapes the real machine never has -
 * two fast regions, a 4KB chip pool - cost nothing to set up.
 *
 * The regions are picked to stay clear of everything else in the harness:
 * the position-independent modules at $100000-$12FFFF, the kernel image at
 * $200000, the scratch allocator at $280000 and the guest stack below
 * $2F0000.
 */
#include "protocol.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MEM_TYPE_END   0
#define MEM_TYPE_CHIP  1
#define MEM_TYPE_FAST  2
#define MEM_TYPE_SLOW  3

#define ALLOC_ANY   0u
#define ALLOC_CHIP  1u
#define ALLOC_FAST  2u
#define ALLOC_SLOW  4u

#define CHIP_BASE   0x140000u
#define CHIP_SIZE   0x040000u
#define FAST_BASE   0x210000u
#define FAST_SIZE   0x040000u
#define FAST2_BASE  0x260000u
#define FAST2_SIZE  0x010000u
#define SLOW_BASE   0xC00000u
#define SLOW_SIZE   0x080000u

/* Bookkeeping the allocator may spend per region before it counts as lost
 * memory: a block header and an end marker, with room to spare. */
#define REGION_OVERHEAD 64u

/* --- calling in ---------------------------------------------------------- */

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

static uint32_t m_alloc(uint32_t size, uint32_t flags)
{
    uint32_t a[2]; a[0] = size; a[1] = flags;
    return kcall("kernel:_mem_alloc", 2, a);
}

static uint32_t m_alloc_tagged(uint32_t size, uint32_t flags, uint32_t owner)
{
    uint32_t a[3]; a[0] = size; a[1] = flags; a[2] = owner;
    return kcall("kernel:_mem_alloc_tagged", 3, a);
}

static int32_t  m_free(uint32_t p)        { return (int32_t)kcall("kernel:_mem_free", 1, &p); }
static uint32_t m_free_owner(uint32_t o)  { return kcall("kernel:_mem_free_owner", 1, &o); }
static uint32_t m_avail(uint32_t pool)    { return kcall("kernel:_mem_avail", 1, &pool); }
static uint32_t m_largest(uint32_t pool)  { return kcall("kernel:_mem_largest", 1, &pool); }
static uint32_t m_check(void)             { return kcall("kernel:_mem_check", 0, NULL); }

/* --- building a map ------------------------------------------------------ */

typedef struct { uint32_t base, size; uint16_t type; } region;

static void init_with(const region *rg, int n, uint32_t kernel_end)
{
    uint8_t buf[12 * 9];
    uint32_t a[2];
    int i;

    memset(buf, 0, sizeof buf);             /* the terminator is all zeros */
    for (i = 0; i < n; i++) {
        uint8_t *e = buf + 12 * i;
        e[0] = (uint8_t)(rg[i].base >> 24); e[1] = (uint8_t)(rg[i].base >> 16);
        e[2] = (uint8_t)(rg[i].base >> 8);  e[3] = (uint8_t)rg[i].base;
        e[4] = (uint8_t)(rg[i].size >> 24); e[5] = (uint8_t)(rg[i].size >> 16);
        e[6] = (uint8_t)(rg[i].size >> 8);  e[7] = (uint8_t)rg[i].size;
        e[8] = (uint8_t)(rg[i].type >> 8);  e[9] = (uint8_t)rg[i].type;
    }
    a[0] = h_alloc(buf, 12u * ((unsigned)n + 1));
    a[1] = kernel_end;
    kcall("kernel:_mem_init", 2, a);
}

/* One chip pool, one fast pool: what the real machine looks like. */
static void init_std(void)
{
    static const region rg[] = {
        { CHIP_BASE, CHIP_SIZE, MEM_TYPE_CHIP },
        { FAST_BASE, FAST_SIZE, MEM_TYPE_FAST },
    };
    init_with(rg, 2, 0);
}

static int inside(uint32_t p, uint32_t size, uint32_t base, uint32_t len)
{
    return p >= base && p + size <= base + len;
}

/* --- init ---------------------------------------------------------------- */

static void t_init_reports_the_regions(void)
{
    init_std();
    CHECK(m_avail(ALLOC_CHIP) <= CHIP_SIZE &&
          m_avail(ALLOC_CHIP) >= CHIP_SIZE - REGION_OVERHEAD,
          "chip avail $%X of $%X", m_avail(ALLOC_CHIP), CHIP_SIZE);
    CHECK(m_avail(ALLOC_FAST) >= FAST_SIZE - REGION_OVERHEAD,
          "fast avail $%X of $%X", m_avail(ALLOC_FAST), FAST_SIZE);
    CHECK_U32(0, m_avail(ALLOC_SLOW));
    CHECK_U32(m_avail(ALLOC_FAST), m_largest(ALLOC_FAST));
    CHECK_U32(0, m_check());
}

/* The ROM reserves the image as loaded from disk, which knows nothing of
 * .bss. kernel_end does, and the heap must start above it. */
static void t_init_keeps_clear_of_the_kernel(void)
{
    static const region rg[] = { { FAST_BASE, FAST_SIZE, MEM_TYPE_FAST } };
    uint32_t kend = FAST_BASE + 0x1235;     /* odd, as _end has been */
    uint32_t p;

    init_with(rg, 1, kend);
    CHECK(m_avail(ALLOC_FAST) <= FAST_SIZE - 0x1235, "heap covers the kernel");
    while ((p = m_alloc(4096, ALLOC_FAST)) != 0)
        if (p < kend) { t_fail("allocated $%X, inside the kernel image", p); return; }
}

static void t_two_regions_in_one_pool(void)
{
    static const region rg[] = {
        { FAST2_BASE, FAST2_SIZE, MEM_TYPE_FAST },      /* small one first */
        { FAST_BASE,  FAST_SIZE,  MEM_TYPE_FAST },
    };
    uint32_t p;

    init_with(rg, 2, 0);
    CHECK(m_avail(ALLOC_FAST) >= FAST_SIZE + FAST2_SIZE - 2 * REGION_OVERHEAD,
          "second region not counted: avail $%X", m_avail(ALLOC_FAST));
    CHECK(m_largest(ALLOC_FAST) < FAST_SIZE, "largest spans two regions");

    p = m_alloc(FAST2_SIZE + 4096, ALLOC_FAST);         /* only fits the big one */
    CHECK(inside(p, FAST2_SIZE + 4096, FAST_BASE, FAST_SIZE),
          "$%X is not in the large region", p);
    CHECK_U32(0, m_check());
}

/* --- alloc --------------------------------------------------------------- */

static void t_alloc_is_aligned_and_disjoint(void)
{
    static const uint32_t sizes[] = { 1, 7, 8, 9, 100, 4096, 3 };
    uint32_t p[7];
    int i, j;

    init_std();
    for (i = 0; i < 7; i++) {
        p[i] = m_alloc(sizes[i], ALLOC_FAST);
        CHECK(p[i] != 0, "alloc(%u) failed", sizes[i]);
        CHECK((p[i] & 7) == 0, "alloc(%u) = $%X, not 8-aligned", sizes[i], p[i]);
        CHECK(inside(p[i], sizes[i], FAST_BASE, FAST_SIZE), "$%X outside the pool", p[i]);
        for (j = 0; j < i; j++)
            CHECK(p[i] + sizes[i] <= p[j] || p[j] + sizes[j] <= p[i],
                  "alloc %d ($%X) overlaps alloc %d ($%X)", i, p[i], j, p[j]);
    }
    CHECK_U32(0, m_check());
}

static void t_alloc_refuses_nonsense(void)
{
    init_std();
    CHECK_U32(0, m_alloc(0, ALLOC_FAST));
    CHECK_U32(0, m_alloc(FAST_SIZE, ALLOC_FAST));       /* no room for a header */
    CHECK_U32(0, m_alloc(0xFFFFFFF9u, ALLOC_FAST));     /* rounds up past zero */
    CHECK_U32(0, m_alloc(0x80000000u, ALLOC_FAST));
    CHECK_U32(0, m_check());
}

/* Each named flag is a requirement, not a preference. */
static void t_flags_pick_the_pool(void)
{
    static const region rg[] = {
        { CHIP_BASE, CHIP_SIZE, MEM_TYPE_CHIP },
        { FAST_BASE, 0x1000,    MEM_TYPE_FAST },        /* tiny on purpose */
        { SLOW_BASE, 0x1000,    MEM_TYPE_SLOW },
    };
    uint32_t p;

    h_attach_slow_ram(SLOW_SIZE);
    init_with(rg, 3, 0);

    p = m_alloc(64, ALLOC_CHIP);
    CHECK(inside(p, 64, CHIP_BASE, CHIP_SIZE), "ALLOC_CHIP gave $%X", p);
    p = m_alloc(64, ALLOC_SLOW);
    CHECK(inside(p, 64, SLOW_BASE, 0x1000), "ALLOC_SLOW gave $%X", p);

    /* ANY: fast while there is some, then slow, then chip. */
    p = m_alloc(3000, ALLOC_ANY);
    CHECK(inside(p, 3000, FAST_BASE, 0x1000), "ALLOC_ANY gave $%X, want fast", p);
    p = m_alloc(3000, ALLOC_ANY);
    CHECK(inside(p, 3000, SLOW_BASE, 0x1000), "ALLOC_ANY gave $%X, want slow", p);
    p = m_alloc(3000, ALLOC_ANY);
    CHECK(inside(p, 3000, CHIP_BASE, CHIP_SIZE), "ALLOC_ANY gave $%X, want chip", p);

    /* Fast is spent, and chip having plenty is no answer to ALLOC_FAST. */
    CHECK_U32(0, m_alloc(3000, ALLOC_FAST));
    CHECK_U32(0, m_check());
}

/* --- free ---------------------------------------------------------------- */

static void t_free_gives_it_all_back(void)
{
    uint32_t before, a, b, c;

    init_std();
    before = m_avail(ALLOC_FAST);
    a = m_alloc(100, ALLOC_FAST);
    b = m_alloc(5000, ALLOC_FAST);
    c = m_alloc(33, ALLOC_FAST);
    CHECK(m_avail(ALLOC_FAST) < before - 5133, "avail did not drop");

    CHECK_U32(0, m_free(b));
    CHECK_U32(0, m_free(a));
    CHECK_U32(0, m_free(c));
    CHECK_U32(before, m_avail(ALLOC_FAST));
    CHECK_U32(before, m_largest(ALLOC_FAST));           /* one block again */
    CHECK_U32(0, m_check());
}

/* Every order of freeing three neighbours has to end in one block: that is
 * merge-with-next, merge-with-previous and merge-with-both all exercised. */
static void t_coalesces_in_every_order(void)
{
    static const int order[6][3] = {
        {0,1,2}, {0,2,1}, {1,0,2}, {1,2,0}, {2,0,1}, {2,1,0}
    };
    int o, i;

    for (o = 0; o < 6; o++) {
        uint32_t p[3], before;

        h_reset();
        init_std();
        before = m_largest(ALLOC_CHIP);
        for (i = 0; i < 3; i++)
            p[i] = m_alloc(1000, ALLOC_CHIP);
        for (i = 0; i < 3; i++)
            CHECK_U32(0, m_free(p[order[o][i]]));
        CHECK(m_largest(ALLOC_CHIP) == before,
              "order %d%d%d left the pool in pieces: largest $%X, was $%X",
              order[o][0], order[o][1], order[o][2], m_largest(ALLOC_CHIP), before);
        CHECK_U32(0, m_check());
    }
}

static void t_free_null_is_a_no_op(void)
{
    init_std();
    CHECK_U32(0, m_free(0));
    CHECK_U32(0, m_check());
}

/* A bad free is a kernel bug, and the allocator's job is to survive it and
 * say so, not to thread garbage into its free list. */
static void t_bad_frees_are_refused(void)
{
    uint32_t p, before;

    init_std();
    p = m_alloc(256, ALLOC_FAST);
    before = m_avail(ALLOC_FAST);

    CHECK(m_free(p + 8) != 0, "freed a pointer into the middle of a block");
    CHECK(m_free(p + 3) != 0, "freed an unaligned pointer");
    CHECK(m_free(CHIP_BASE - 0x1000) != 0, "freed a pointer outside every pool");
    CHECK(m_free(0x00DFF000u) != 0, "freed a custom chip register");
    CHECK_U32(before, m_avail(ALLOC_FAST));

    CHECK_U32(0, m_free(p));
    CHECK(m_free(p) != 0, "double free accepted");
    CHECK_U32(0, m_check());
}

/* --- policy -------------------------------------------------------------- */

/* Chip is best-fit: a small request goes in the small hole and leaves the
 * big one whole for the bitmap that needs it. */
static void t_chip_is_best_fit(void)
{
    uint32_t big, small, p;

    init_std();
    big   = m_alloc(8192, ALLOC_CHIP);  m_alloc(16, ALLOC_CHIP);
    small = m_alloc(256,  ALLOC_CHIP);  m_alloc(16, ALLOC_CHIP);
    m_free(small);
    m_free(big);

    p = m_alloc(200, ALLOC_CHIP);
    CHECK_U32(small, p);
    CHECK(m_largest(ALLOC_CHIP) >= 8192, "the big hole was broken up");
}

static void t_exhaust_and_recover(void)
{
    static uint32_t p[4096];
    uint32_t before;
    int n = 0, i;

    init_std();
    before = m_avail(ALLOC_CHIP);
    while (n < 4096 && (p[n] = m_alloc(1000, ALLOC_CHIP)) != 0)
        n++;
    CHECK(n > 200 && n < 4096, "got %d blocks of 1000 out of $%X", n, CHIP_SIZE);
    CHECK(m_avail(ALLOC_CHIP) < 1024, "gave up with $%X free", m_avail(ALLOC_CHIP));
    CHECK_U32(0, m_check());

    for (i = 0; i < n; i += 2) m_free(p[i]);            /* swiss cheese first */
    for (i = 1; i < n; i += 2) m_free(p[i]);
    CHECK_U32(before, m_avail(ALLOC_CHIP));
    CHECK_U32(before, m_largest(ALLOC_CHIP));
    CHECK_U32(0, m_check());
}

/* --- ownership ----------------------------------------------------------- */

static void t_free_owner_takes_only_its_own(void)
{
    uint32_t before, keep, i;

    init_std();
    before = m_avail(ALLOC_FAST);
    for (i = 0; i < 5; i++) m_alloc_tagged(100 + i, ALLOC_FAST, 0x1111);
    keep = m_alloc_tagged(64, ALLOC_FAST, 0x2222);
    for (i = 0; i < 3; i++) m_alloc_tagged(500, ALLOC_CHIP, 0x1111);
    h_poke32(keep, 0xFEEDFACEu);

    CHECK_U32(8, m_free_owner(0x1111));                 /* both pools */
    CHECK_U32(0xFEEDFACEu, h_peek32(keep));
    CHECK_U32(0, m_free_owner(0x1111));                 /* nothing left */
    CHECK_U32(0, m_free(keep));
    CHECK_U32(before, m_avail(ALLOC_FAST));
    CHECK_U32(0, m_check());
}

/* Owner 0 is the kernel, and the kernel does not exit. */
static void t_free_owner_never_takes_the_kernels(void)
{
    init_std();
    m_alloc(100, ALLOC_FAST);
    CHECK_U32(0, m_free_owner(0));
}

/* --- integrity ----------------------------------------------------------- */

static void t_check_sees_an_overrun(void)
{
    uint32_t a, b;

    init_std();
    a = m_alloc(64, ALLOC_FAST);
    b = m_alloc(64, ALLOC_FAST);
    CHECK_U32(0, m_check());

    /* Whichever of the two is higher has its header just past the other's
     * payload. Write well past the end of the lower one. */
    {
        uint32_t lo = a < b ? a : b, i;
        for (i = 64; i < 96; i += 4)
            h_poke32(lo + i, 0x5A5A5A5Au);
    }
    CHECK(m_check() != 0, "mem_check passed a trampled header");
    CHECK(m_free(a < b ? b : a) != 0, "freed a block with a trampled header");
}

static void t_safe_with_interrupts_live(void)
{
    uint32_t p;

    init_std();
    h_set_sr(0x2300);
    p = m_alloc(64, ALLOC_FAST);
    CHECK_U32(0x2300, h_get_sr() & 0xFF00);
    h_set_sr(0x2300);
    m_free(p);
    CHECK_U32(0x2300, h_get_sr() & 0xFF00);
}

/*
 * The one that finds what the others did not think of. A host-side model
 * tracks every live block and its fill byte; every block is verified before
 * it is freed, so an allocator that hands out overlapping memory or lets
 * its own bookkeeping leak into a payload shows up as a wrong byte.
 */
static void t_random_churn(void)
{
    enum { SLOTS = 96, OPS = 3000 };
    static struct { uint32_t p, size, pool; uint8_t fill; } live[SLOTS];
    uint32_t seed = 0xC0FFEEu, before_chip, before_fast;
    int op, i;

    memset(live, 0, sizeof live);
    init_std();
    before_chip = m_avail(ALLOC_CHIP);
    before_fast = m_avail(ALLOC_FAST);

    for (op = 0; op < OPS && !t_failed(); op++) {
        int s;
        seed = seed * 1664525u + 1013904223u;
        s = (int)((seed >> 16) % SLOTS);

        if (live[s].p) {
            uint32_t k;
            for (k = 0; k < live[s].size; k++)
                if (h_peek8(live[s].p + k) != live[s].fill) {
                    t_fail("op %d: block $%X+%u corrupted", op, live[s].p, k);
                    return;
                }
            CHECK_U32(0, m_free(live[s].p));
            live[s].p = 0;
        } else {
            uint32_t r = seed >> 8, k;
            live[s].size = (r % 7 == 0) ? 1 + r % 9000 : 1 + r % 300;
            live[s].pool = (r & 0x10000) ? ALLOC_CHIP : ALLOC_FAST;
            live[s].fill = (uint8_t)(op | 1);
            live[s].p = m_alloc(live[s].size, live[s].pool);
            for (k = 0; live[s].p && k < live[s].size; k++)
                h_poke8(live[s].p + k, live[s].fill);
        }
        if (op % 100 == 0)
            CHECK(m_check() == 0, "op %d: mem_check failed with %u", op, m_check());
    }

    for (i = 0; i < SLOTS; i++)
        if (live[i].p) m_free(live[i].p);
    CHECK_U32(before_chip, m_avail(ALLOC_CHIP));
    CHECK_U32(before_fast, m_avail(ALLOC_FAST));
    CHECK_U32(before_chip, m_largest(ALLOC_CHIP));
    CHECK_U32(0, m_check());
}

/* ------------------------------------------------------------------------ */

static const test_case tests[] = {
    { "init_reports_regions",   t_init_reports_the_regions,         NULL },
    { "init_clear_of_kernel",   t_init_keeps_clear_of_the_kernel,   NULL },
    { "two_regions_one_pool",   t_two_regions_in_one_pool,          NULL },
    { "alloc_aligned_disjoint", t_alloc_is_aligned_and_disjoint,    NULL },
    { "alloc_refuses_nonsense", t_alloc_refuses_nonsense,           NULL },
    { "flags_pick_the_pool",    t_flags_pick_the_pool,              NULL },
    { "free_gives_it_back",     t_free_gives_it_all_back,           NULL },
    { "coalesce_every_order",   t_coalesces_in_every_order,         NULL },
    { "free_null",              t_free_null_is_a_no_op,             NULL },
    { "bad_frees_refused",      t_bad_frees_are_refused,            NULL },
    { "chip_best_fit",          t_chip_is_best_fit,                 NULL },
    { "exhaust_and_recover",    t_exhaust_and_recover,              NULL },
    { "free_owner",             t_free_owner_takes_only_its_own,    NULL },
    { "free_owner_not_kernel",  t_free_owner_never_takes_the_kernels, NULL },
    { "check_sees_overrun",     t_check_sees_an_overrun,            NULL },
    { "safe_with_interrupts",   t_safe_with_interrupts_live,        NULL },
    { "random_churn",           t_random_churn,                     NULL },
};

const test_suite kmem_suite = { "kmem", tests, sizeof tests / sizeof tests[0] };
