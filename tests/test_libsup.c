/*
 * test_libsup.c - src/kernel/libsup.s
 *
 * vbcc emits calls to these for every 32-bit divide and modulo, because the
 * 68000 has neither. They are hand-written assembly that has never been
 * exercised: the kernel they belong to has a bump allocator and a printf,
 * and printf is where the first %u would reach them.
 *
 * libsup.s is position independent - only PC-relative branches, no data
 * references - so it is assembled standalone to origin zero, loaded at
 * LIBSUP_BASE, and its symbols merged with a matching bias.
 *
 * Convention, from the file's own header:
 *   D0 = dividend / left operand
 *   D1 = divisor / right operand
 *   result in D0, D1 may be trashed
 */
#include "protocol.h"

#include <stdint.h>

static uint32_t call2(const char *name, uint32_t a, uint32_t b)
{
    h_result r;
    h_begin_call();
    h_set_d(0, a);
    h_set_d(1, b);
    r = h_call(h_sym(name));
    CHECK_CALL(r);
    return h_get_d(0);
}

static uint32_t divu(uint32_t a, uint32_t b) { return call2("__divu", a, b); }
static uint32_t modu(uint32_t a, uint32_t b) { return call2("__modu", a, b); }
static int32_t  divs(int32_t a, int32_t b)
{ return (int32_t)call2("__divs", (uint32_t)a, (uint32_t)b); }
static int32_t  mods(int32_t a, int32_t b)
{ return (int32_t)call2("__mods", (uint32_t)a, (uint32_t)b); }

static void ck_divu(uint32_t a, uint32_t b)
{
    uint32_t got = divu(a, b), want = a / b;
    if (got != want)
        t_fail("__divu %u / %u: expected %u got %u", a, b, want, got);
}

static void ck_modu(uint32_t a, uint32_t b)
{
    uint32_t got = modu(a, b), want = a % b;
    if (got != want)
        t_fail("__modu %u %% %u: expected %u got %u", a, b, want, got);
}

static void ck_divs(int32_t a, int32_t b)
{
    int32_t got = divs(a, b), want = a / b;
    if (got != want)
        t_fail("__divs %ld / %ld: expected %ld got %ld",
               (long)a, (long)b, (long)want, (long)got);
}

static void ck_mods(int32_t a, int32_t b)
{
    int32_t got = mods(a, b), want = a % b;
    if (got != want)
        t_fail("__mods %ld %% %ld: expected %ld got %ld",
               (long)a, (long)b, (long)want, (long)got);
}

/* --- __divu ------------------------------------------------------------- */

static void t_divu_basic(void)
{
    ck_divu(0, 1);
    ck_divu(6, 2);
    ck_divu(100, 7);
    ck_divu(1, 1);
    ck_divu(9, 10);
}

static void t_divu_fast_path(void)
{
    /* Divisor under 65536 and quotient under 65536: the single divu.w. */
    ck_divu(65535, 1);
    ck_divu(65535, 255);
    ck_divu(1000000u, 100u);
}

static void t_divu_quotient_over_16_bits(void)
{
    /* Forces the shift-subtract path: divu.w could not hold this quotient. */
    ck_divu(65536u, 1u);
    ck_divu(0xFFFFFFFFu, 1u);
    ck_divu(0xFFFFFFFFu, 3u);
    ck_divu(1000000u, 3u);
}

static void t_divu_divisor_over_16_bits(void)
{
    /* Any divisor at or above 65536 also takes the shift-subtract path. */
    ck_divu(0x10000000u, 0x20000u);
    ck_divu(0xFFFFFFFFu, 0x10000u);
    ck_divu(0x12345678u, 0x1234u);
    ck_divu(0x12345678u, 0x123456u);
}

static void t_divu_divisor_above_2_31(void)
{
    /* The remainder register is the same width as the divisor, so shifting
     * a remainder that is already above 2^31 loses its top bit. */
    ck_divu(0xFFFFFFFFu, 0x80000000u);
    ck_divu(0xFFFFFFFFu, 0x80000001u);
    ck_divu(0xFFFFFFFFu, 0xC0000000u);
    ck_divu(0xFFFFFFFFu, 0xFFFFFFFFu);
    ck_divu(0x80000000u, 0x80000000u);
}

static void t_divu_by_zero(void)
{
    /* Documented: "Division by zero - return max value". */
    CHECK_U32(0xFFFFFFFFu, divu(12345, 0));
}

/* --- __modu ------------------------------------------------------------- */

static void t_modu_basic(void)
{
    ck_modu(0, 1);
    ck_modu(7, 2);
    ck_modu(100, 7);
    ck_modu(9, 10);
}

static void t_modu_fast_path(void)
{
    ck_modu(65535, 255);
    ck_modu(1000000u, 100u);
}

static void t_modu_quotient_over_16_bits(void)
{
    ck_modu(0xFFFFFFFFu, 3u);
    ck_modu(1000000u, 3u);
    ck_modu(0xFFFFFFFFu, 10u);
}

static void t_modu_divisor_over_16_bits(void)
{
    ck_modu(0x12345678u, 0x1234u);
    ck_modu(0x12345678u, 0x123456u);
    ck_modu(0xFFFFFFFFu, 0x10000u);
}

static void t_modu_divisor_above_2_31(void)
{
    ck_modu(0xFFFFFFFFu, 0x80000001u);
    ck_modu(0xFFFFFFFFu, 0xC0000000u);
    ck_modu(0x80000000u, 0x80000000u);
}

static void t_modu_by_zero(void)
{
    /* Documented: returns zero. */
    CHECK_U32(0u, modu(12345, 0));
}

/* --- __divs / __mods ---------------------------------------------------- */

static void t_divs_signs(void)
{
    /* C truncates toward zero. */
    ck_divs(7, 2);
    ck_divs(-7, 2);
    ck_divs(7, -2);
    ck_divs(-7, -2);
    ck_divs(0, -1);
}

static void t_divs_large(void)
{
    ck_divs(1000000, 3);
    ck_divs(-1000000, 3);
    ck_divs(2147483647, 3);
    ck_divs(-2147483647, 3);
    ck_divs(-2147483647, -1);
}

static void t_mods_sign_follows_dividend(void)
{
    ck_mods(7, 2);
    ck_mods(-7, 2);
    ck_mods(7, -2);
    ck_mods(-7, -2);
}

static void t_mods_large(void)
{
    ck_mods(1000000, 7);
    ck_mods(-1000000, 7);
    ck_mods(2147483647, 10);
    ck_mods(-2147483647, 10);
}

/* --- sweeps ------------------------------------------------------------- */

static uint32_t lcg(uint32_t *s) { *s = *s * 1103515245u + 12345u; return *s; }

static void t_divu_sweep(void)
{
    uint32_t state = 99991u;
    int i;
    for (i = 0; i < 1500; i++) {
        uint32_t a = lcg(&state);
        uint32_t b = lcg(&state);
        if (b == 0) b = 1;
        ck_divu(a, b);
        if (t_failed()) return;
    }
}

static void t_modu_sweep(void)
{
    uint32_t state = 4242u;
    int i;
    for (i = 0; i < 1500; i++) {
        uint32_t a = lcg(&state);
        uint32_t b = lcg(&state);
        if (b == 0) b = 1;
        ck_modu(a, b);
        if (t_failed()) return;
    }
}

static void t_divs_sweep(void)
{
    uint32_t state = 13579u;
    int i;
    for (i = 0; i < 1500; i++) {
        int32_t a = (int32_t)lcg(&state);
        int32_t b = (int32_t)lcg(&state);
        if (b == 0) b = 1;
        if (a == (-2147483647 - 1) && b == -1) continue;   /* undefined in C */
        ck_divs(a, b);
        if (t_failed()) return;
    }
}

static void t_mods_sweep(void)
{
    uint32_t state = 24680u;
    int i;
    for (i = 0; i < 1500; i++) {
        int32_t a = (int32_t)lcg(&state);
        int32_t b = (int32_t)lcg(&state);
        if (b == 0) b = 1;
        if (a == (-2147483647 - 1) && b == -1) continue;
        ck_mods(a, b);
        if (t_failed()) return;
    }
}

/* ------------------------------------------------------------------------ */

static const test_case tests[] = {
    { "divu_basic",                 t_divu_basic,                 NULL },
    { "divu_fast_path",             t_divu_fast_path,             NULL },
    { "divu_quotient_over_16_bits", t_divu_quotient_over_16_bits, NULL },
    { "divu_divisor_over_16_bits",  t_divu_divisor_over_16_bits,  NULL },
    { "divu_divisor_above_2_31",    t_divu_divisor_above_2_31,    NULL },
    { "divu_by_zero",               t_divu_by_zero,               NULL },

    { "modu_basic",                 t_modu_basic,                 NULL },
    { "modu_fast_path",             t_modu_fast_path,             NULL },
    { "modu_quotient_over_16_bits", t_modu_quotient_over_16_bits, NULL },
    { "modu_divisor_over_16_bits",  t_modu_divisor_over_16_bits,  NULL },
    { "modu_divisor_above_2_31",    t_modu_divisor_above_2_31,    NULL },
    { "modu_by_zero",               t_modu_by_zero,               NULL },

    { "divs_signs",                 t_divs_signs,                 NULL },
    { "divs_large",                 t_divs_large,                 NULL },
    { "mods_sign_follows_dividend", t_mods_sign_follows_dividend, NULL },
    { "mods_large",                 t_mods_large,                 NULL },

    { "divu_sweep",                 t_divu_sweep,                 NULL },
    { "modu_sweep",                 t_modu_sweep,                 NULL },
    { "divs_sweep",                 t_divs_sweep,                 NULL },
    { "mods_sweep",                 t_mods_sweep,                 NULL },
};

const test_suite libsup_suite = { "libsup", tests, sizeof tests / sizeof tests[0] };
