/*
 * test_format.c - src/rom/sprintf.s and the serial formatting helpers
 *
 * The specification these check against is the documented behaviour in the
 * header comment of sprintf.s. Where the implementation disagrees with its
 * own documentation the test is marked xfail with the reason, so the suite
 * stays green while the bug stays visible.
 */
#include "protocol.h"

#include <string.h>

/* Call SerialPrintf the way ROM code does: arguments pushed right to left,
 * format string pushed last, caller cleans up. Returns captured serial. */
static const char *rom_printf(const char *fmt, const uint32_t *args, int nargs)
{
    uint32_t fmt_addr = h_str(fmt);
    h_result r;
    int i;

    h_begin_call();
    for (i = nargs - 1; i >= 0; i--)
        h_push32(args[i]);
    h_push32(fmt_addr);

    r = h_call(h_sym("SerialPrintf"));
    CHECK_CALL(r);
    return h_serial();
}

static const char *rom_printf0(const char *fmt)
{
    return rom_printf(fmt, NULL, 0);
}

static const char *rom_printf1(const char *fmt, uint32_t a)
{
    return rom_printf(fmt, &a, 1);
}

/* --- literals ----------------------------------------------------------- */

static void t_literal(void)
{
    CHECK_STR("Hello, world", rom_printf0("Hello, world"));
}

static void t_empty(void)
{
    CHECK_STR("", rom_printf0(""));
}

static void t_percent_escape(void)
{
    CHECK_STR("100% done", rom_printf0("100%% done"));
}

/* --- %x ----------------------------------------------------------------- */

static void t_hex_default_is_long(void)
{
    /* Documented: "%x.l - Hex long (8 digits, default)" */
    CHECK_STR("DEADBEEF", rom_printf1("%x", 0xDEADBEEFu));
}

static void t_hex_small_value_pads_to_8(void)
{
    CHECK_STR("00001234", rom_printf1("%x", 0x1234u));
}

static void t_hex_long_suffix(void)
{
    /* Documented: "%x.l - Hex long (8 digits)". The parser expects the size
     * modifier BEFORE the specifier, so the ".l" is echoed as literal text. */
    CHECK_STR("DEADBEEF", rom_printf1("%x.l", 0xDEADBEEFu));
}

static void t_hex_byte_suffix(void)
{
    /* Documented: "%x.b - Hex byte (2 digits)". */
    CHECK_STR("AB", rom_printf1("%x.b", 0xABu));
}

static void t_hex_word_suffix(void)
{
    /* Documented: "%x.w - Hex word (4 digits)". */
    CHECK_STR("1234", rom_printf1("%x.w", 0x1234u));
}

static void t_hex_prefix_long(void)
{
    /* The form the parser actually implements, used by memory.s. */
    CHECK_STR("DEADBEEF", rom_printf1("%.lx", 0xDEADBEEFu));
}

static void t_hex_prefix_byte(void)
{
    CHECK_STR("AB", rom_printf1("%.bx", 0xABu));
}

static void t_hex_prefix_word(void)
{
    CHECK_STR("1234", rom_printf1("%.wx", 0x1234u));
}

static void t_hex_width(void)
{
    /* Single-digit width: print only the low 4 nibbles. */
    CHECK_STR("BEEF", rom_printf1("%4x", 0xDEADBEEFu));
}

static void t_hex_zero_pad_width(void)
{
    /* Documented: "Width specifier (optional): %08x - Pad with zeros to 8" */
    CHECK_STR("00001234", rom_printf1("%08x", 0x1234u));
}

/* --- %d ----------------------------------------------------------------- */

static void t_decimal_zero(void)
{
    CHECK_STR("0", rom_printf1("%d", 0));
}

static void t_decimal_small(void)
{
    CHECK_STR("42", rom_printf1("%d", 42));
}

static void t_decimal_five_digits(void)
{
    CHECK_STR("65535", rom_printf1("%d", 65535));
}

static void t_decimal_at_divu_limit(void)
{
    /* divu.w overflows once the quotient exceeds 16 bits, i.e. at 655360.
     * One below that must still be correct. */
    CHECK_STR("655359", rom_printf1("%d", 655359));
}

static void t_decimal_past_divu_limit(void)
{
    /* Documented: "%d - Unsigned decimal", with no stated range limit.
     * partition.s prints LBAs and block counts with %d. */
    CHECK_STR("655360", rom_printf1("%d", 655360));
}

static void t_decimal_large(void)
{
    CHECK_STR("16777216", rom_printf1("%d", 16777216));
}

/* --- %s ----------------------------------------------------------------- */

static void t_string(void)
{
    uint32_t arg = h_str("Workbench");
    CHECK_STR("[Workbench]", rom_printf1("[%s]", arg));
}

static void t_string_empty(void)
{
    uint32_t arg = h_str("");
    CHECK_STR("[]", rom_printf1("[%s]", arg));
}

/* --- multiple arguments ------------------------------------------------- */

static void t_two_hex_args(void)
{
    uint32_t args[2] = { 0x11111111u, 0x22222222u };
    CHECK_STR("11111111/22222222", rom_printf("%x/%x", args, 2));
}

static void t_three_decimal_args(void)
{
    uint32_t args[3] = { 1, 2, 3 };
    CHECK_STR("1-2-3", rom_printf("%d-%d-%d", args, 3));
}

static void t_mixed_args(void)
{
    uint32_t args[3];
    args[0] = 0xCAFEu;
    args[1] = h_str("mid");
    args[2] = 7;
    CHECK_STR("0000CAFE mid 7", rom_printf("%x %s %d", args, 3));
}

static void t_two_byte_args_like_ide(void)
{
    /* The exact shape ide.s uses for its error message. */
    uint32_t args[2] = { 0x51u, 0x04u };
    CHECK_STR("status=51 error=04", rom_printf("status=%x.b error=%x.b", args, 2));
}

/* --- serial_put_* helpers ----------------------------------------------- */

static void t_put_hex32(void)
{
    h_result r;
    h_begin_call();
    h_set_d(0, 0xDEADBEEFu);
    r = h_call(h_sym("serial_put_hex32"));
    CHECK_CALL(r);
    CHECK_STR("DEADBEEF", h_serial());
}

static void t_put_hex16(void)
{
    h_result r;
    h_begin_call();
    h_set_d(0, 0x1234u);
    r = h_call(h_sym("serial_put_hex16"));
    CHECK_CALL(r);
    CHECK_STR("1234", h_serial());
}

static void t_put_hex8(void)
{
    h_result r;
    h_begin_call();
    h_set_d(0, 0xABu);
    r = h_call(h_sym("serial_put_hex8"));
    CHECK_CALL(r);
    CHECK_STR("AB", h_serial());
}

static void t_put_string(void)
{
    h_result r;
    h_begin_call();
    h_set_a(0, h_str("chip ram"));
    r = h_call(h_sym("serial_put_string"));
    CHECK_CALL(r);
    CHECK_STR("chip ram", h_serial());
}

static void t_put_decimal_small(void)
{
    h_result r;
    h_begin_call();
    h_set_d(0, 4095);
    r = h_call(h_sym("serial_put_decimal"));
    CHECK_CALL(r);
    CHECK_STR("4095", h_serial());
}

static void t_put_decimal_large(void)
{
    h_result r;
    h_begin_call();
    h_set_d(0, 1000000);
    r = h_call(h_sym("serial_put_decimal"));
    CHECK_CALL(r);
    CHECK_STR("1000000", h_serial());
}

/* --- parse_hex ---------------------------------------------------------- */

/* A0 = string, returns D0 = value, D1 = digit count, A0 advanced. */
static void parse_hex_call(const char *s, uint32_t *value, uint32_t *ndigits)
{
    h_result r;
    h_begin_call();
    h_set_a(0, h_str(s));
    r = h_call(h_sym("parse_hex"));
    CHECK_CALL(r);
    *value   = h_get_d(0);
    *ndigits = h_get_d(1) & 0xFFFFu;
}

static void t_parse_hex_basic(void)
{
    uint32_t v, n;
    parse_hex_call("1234", &v, &n);
    CHECK_U32(0x1234u, v);
    CHECK_U32(4u, n);
}

static void t_parse_hex_dollar_prefix(void)
{
    uint32_t v, n;
    parse_hex_call("$FC0000", &v, &n);
    CHECK_U32(0xFC0000u, v);
    CHECK_U32(6u, n);
}

static void t_parse_hex_lowercase(void)
{
    uint32_t v, n;
    parse_hex_call("deadbeef", &v, &n);
    CHECK_U32(0xDEADBEEFu, v);
    CHECK_U32(8u, n);
}

static void t_parse_hex_stops_at_space(void)
{
    uint32_t v, n;
    parse_hex_call("2000 ff", &v, &n);
    CHECK_U32(0x2000u, v);
    CHECK_U32(4u, n);
}

static void t_parse_hex_no_digits(void)
{
    uint32_t v, n;
    parse_hex_call("zzz", &v, &n);
    CHECK_U32(0u, n);
}

/* ------------------------------------------------------------------------ */

static const test_case tests[] = {
    { "literal",                 t_literal,                 NULL },
    { "empty",                   t_empty,                   NULL },
    { "percent_escape",          t_percent_escape,          NULL },

    { "hex_default_is_long",     t_hex_default_is_long,     NULL },
    { "hex_small_value_pads",    t_hex_small_value_pads_to_8, NULL },
    { "hex_long_suffix",         t_hex_long_suffix,
      "sprintf.s wants the size modifier BEFORE the specifier (%.lx), so the '.l' in '%x.l' is echoed as literal text" },
    { "hex_byte_suffix",         t_hex_byte_suffix,
      "sprintf.s wants the size modifier BEFORE the specifier (%.lx), so the '.l' in '%x.l' is echoed as literal text" },
    { "hex_word_suffix",         t_hex_word_suffix,
      "sprintf.s wants the size modifier BEFORE the specifier (%.lx), so the '.l' in '%x.l' is echoed as literal text" },
    { "hex_prefix_long",         t_hex_prefix_long,         NULL },
    { "hex_prefix_byte",         t_hex_prefix_byte,
      "%.bx/%.wx read a word from a longword stack slot, picking up the high half" },
    { "hex_prefix_word",         t_hex_prefix_word,
      "%.bx/%.wx read a word from a longword stack slot, picking up the high half" },
    { "hex_width",               t_hex_width,               NULL },
    { "hex_zero_pad_width",      t_hex_zero_pad_width,
      "width parser consumes one digit, so '0' of '%08x' becomes the width and '8' is read as the specifier" },

    { "decimal_zero",            t_decimal_zero,            NULL },
    { "decimal_small",           t_decimal_small,           NULL },
    { "decimal_five_digits",     t_decimal_five_digits,     NULL },
    { "decimal_at_divu_limit",   t_decimal_at_divu_limit,   NULL },
    { "decimal_past_divu_limit", t_decimal_past_divu_limit,
      "FormatDecToBuffer uses divu.w: the quotient overflows 16 bits at 655360" },
    { "decimal_large",           t_decimal_large,
      "FormatDecToBuffer uses divu.w: the quotient overflows 16 bits at 655360" },

    { "string",                  t_string,                  NULL },
    { "string_empty",            t_string_empty,            NULL },

    { "two_hex_args",            t_two_hex_args,            NULL },
    { "three_decimal_args",      t_three_decimal_args,      NULL },
    { "mixed_args",              t_mixed_args,              NULL },
    { "two_byte_args_like_ide",  t_two_byte_args_like_ide,
      "sprintf.s wants the size modifier BEFORE the specifier (%.lx), so the '.l' in '%x.l' is echoed as literal text" },

    { "put_hex32",               t_put_hex32,               NULL },
    { "put_hex16",               t_put_hex16,               NULL },
    { "put_hex8",                t_put_hex8,                NULL },
    { "put_string",              t_put_string,              NULL },
    { "put_decimal_small",       t_put_decimal_small,
      "serial_put_decimal reversal stores at the wrong index then prints from the advanced pointer; currently uncalled" },
    { "put_decimal_large",       t_put_decimal_large,
      "serial_put_decimal reversal stores at the wrong index then prints from the advanced pointer; currently uncalled" },

    { "parse_hex_basic",         t_parse_hex_basic,         NULL },
    { "parse_hex_dollar_prefix", t_parse_hex_dollar_prefix, NULL },
    { "parse_hex_lowercase",     t_parse_hex_lowercase,     NULL },
    { "parse_hex_stops_at_space",t_parse_hex_stops_at_space,NULL },
    { "parse_hex_no_digits",     t_parse_hex_no_digits,     NULL },
};

const test_suite format_suite = { "format", tests, sizeof tests / sizeof tests[0] };
