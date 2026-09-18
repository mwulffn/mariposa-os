/*
 * test_math.c - src/rom/math.s
 *
 * The 32-bit multiply and divide-by-10 that partition.s and sprintf.s need
 * because the 68000 has neither. Both replaced instructions that failed
 * quietly on overflow, so the boundaries are the point of these tests.
 */
#include "protocol.h"

/* D0.l = multiplicand, D1.w = multiplier -> D0.l */
static uint32_t mul32x16(uint32_t a, uint32_t b)
{
    h_result r;
    h_begin_call();
    h_set_d(0, a);
    h_set_d(1, b);
    r = h_call(h_sym("mul32x16"));
    CHECK_CALL(r);
    return h_get_d(0);
}

/* D0.l = dividend -> D0.l quotient, D1.l remainder */
static void divu32_10(uint32_t n, uint32_t *q, uint32_t *rem)
{
    h_result r;
    h_begin_call();
    h_set_d(0, n);
    r = h_call(h_sym("divu32_10"));
    CHECK_CALL(r);
    *q = h_get_d(0);
    *rem = h_get_d(1);
}

/* --- mul32x16 ----------------------------------------------------------- */

static void t_mul_zero(void)      { CHECK_U32(0u, mul32x16(0, 12345)); }
static void t_mul_by_zero(void)   { CHECK_U32(0u, mul32x16(12345, 0)); }
static void t_mul_by_one(void)    { CHECK_U32(0xDEADBEEFu, mul32x16(0xDEADBEEFu, 1)); }
static void t_mul_small(void)     { CHECK_U32(600u, mul32x16(50, 12)); }

static void t_mul_crosses_16_bits(void)
{
    /* The exact product that the old chained mulu.w truncated: a partition
     * at cylinder 20000 on a 4-head disk. */
    CHECK_U32(80000u, mul32x16(20000, 4));
}

static void t_mul_uses_high_word(void)
{
    /* Multiplicand above 65535, so the high-word half of the routine has to
     * contribute. 65536 * 3 = 196608. */
    CHECK_U32(196608u, mul32x16(65536, 3));
}

static void t_mul_large(void)
{
    CHECK_U32(2560000u, mul32x16(80000, 32));      /* the full LBA of the bigcyl image */
    CHECK_U32(5000000u, mul32x16(100000, 50));
}

static void t_mul_word_limits(void)
{
    CHECK_U32(0xFFFE0001u, mul32x16(0xFFFFu, 0xFFFFu));
    CHECK_U32(0xFFFF0000u, mul32x16(0x00010000u, 0xFFFFu));
}

static void t_mul_wraps_at_32_bits(void)
{
    /* Documented behaviour: the low 32 bits of the product, no carry out. */
    CHECK_U32(0x00000000u, mul32x16(0x80000000u, 2));
    CHECK_U32(0xFFFFFFFEu, mul32x16(0x7FFFFFFFu, 2));
}

static void t_mul_ignores_multiplier_high_word(void)
{
    /* D1.w is the multiplier; the high word is documented as ignored. */
    CHECK_U32(600u, mul32x16(50, 0xABCD000Cu));
}

/* --- divu32_10 ---------------------------------------------------------- */

static void check_div(uint32_t n, uint32_t eq, uint32_t er)
{
    uint32_t q, r;
    divu32_10(n, &q, &r);
    if (q != eq || r != er)
        t_fail("%u / 10: expected %u r %u, got %u r %u", n, eq, er, q, r);
}

static void t_div_zero(void)        { check_div(0, 0, 0); }
static void t_div_below_ten(void)   { check_div(9, 0, 9); }
static void t_div_exactly_ten(void) { check_div(10, 1, 0); }
static void t_div_small(void)       { check_div(4095, 409, 5); }

static void t_div_at_divu_w_limit(void)
{
    /* 655359 is the largest dividend whose quotient still fits in 16 bits,
     * which is as far as the old divu.w #10 could go. */
    check_div(655359u, 65535u, 9u);
}

static void t_div_past_divu_w_limit(void)
{
    /* One past it. divu.w overflowed here and left its destination alone. */
    check_div(655360u, 65536u, 0u);
}

static void t_div_large(void)
{
    check_div(1000000u, 100000u, 0u);
    check_div(16777216u, 1677721u, 6u);
}

static void t_div_max_u32(void)
{
    check_div(0xFFFFFFFFu, 429496729u, 5u);
}

static void t_div_every_remainder(void)
{
    /* All ten remainders, above the old overflow point so the two-step path
     * is the one being exercised. */
    uint32_t base = 1000000u;
    uint32_t i;
    for (i = 0; i < 10; i++)
        check_div(base + i, 100000u, i);
}

/* --- sweeps ------------------------------------------------------------- */

/* Hand-written assembly with a hand-picked set of boundary cases is exactly
 * where an off-by-one hides. Sweep both routines against host arithmetic,
 * which is the independent implementation the boundary tests lack. */

static uint32_t lcg(uint32_t *state)
{
    *state = *state * 1103515245u + 12345u;
    return *state;
}

static void t_div_sweep(void)
{
    uint32_t state = 12345u;
    int i;

    for (i = 0; i < 2000; i++) {
        uint32_t n = lcg(&state), q, r;
        divu32_10(n, &q, &r);
        if (q != n / 10u || r != n % 10u) {
            t_fail("sweep: %u / 10 expected %u r %u, got %u r %u",
                   n, n / 10u, n % 10u, q, r);
            return;
        }
    }
}

static void t_mul_sweep(void)
{
    uint32_t state = 6789u;
    int i;

    for (i = 0; i < 2000; i++) {
        uint32_t a = lcg(&state);
        uint32_t b = lcg(&state) & 0xFFFFu;
        uint32_t want = a * b;          /* wraps mod 2^32, same as the routine */
        uint32_t got = mul32x16(a, b);
        if (got != want) {
            t_fail("sweep: %u * %u expected $%08X got $%08X", a, b, want, got);
            return;
        }
    }
}

/* ------------------------------------------------------------------------ */

static const test_case tests[] = {
    { "mul_zero",                      t_mul_zero,                      NULL },
    { "mul_by_zero",                   t_mul_by_zero,                   NULL },
    { "mul_by_one",                    t_mul_by_one,                    NULL },
    { "mul_small",                     t_mul_small,                     NULL },
    { "mul_crosses_16_bits",           t_mul_crosses_16_bits,           NULL },
    { "mul_uses_high_word",            t_mul_uses_high_word,            NULL },
    { "mul_large",                     t_mul_large,                     NULL },
    { "mul_word_limits",               t_mul_word_limits,               NULL },
    { "mul_wraps_at_32_bits",          t_mul_wraps_at_32_bits,          NULL },
    { "mul_ignores_multiplier_high",   t_mul_ignores_multiplier_high_word, NULL },

    { "div_zero",                      t_div_zero,                      NULL },
    { "div_below_ten",                 t_div_below_ten,                 NULL },
    { "div_exactly_ten",               t_div_exactly_ten,               NULL },
    { "div_small",                     t_div_small,                     NULL },
    { "div_at_divu_w_limit",           t_div_at_divu_w_limit,           NULL },
    { "div_past_divu_w_limit",         t_div_past_divu_w_limit,         NULL },
    { "div_large",                     t_div_large,                     NULL },
    { "div_max_u32",                   t_div_max_u32,                   NULL },
    { "div_every_remainder",           t_div_every_remainder,           NULL },

    { "mul_sweep",                     t_mul_sweep,                     NULL },
    { "div_sweep",                     t_div_sweep,                     NULL },
};

const test_suite math_suite = { "math", tests, sizeof tests / sizeof tests[0] };
