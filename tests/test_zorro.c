/*
 * test_zorro.c - Zorro II autoconfig
 *
 * Written against the assembly before it was converted. The memory-card path
 * runs on every real boot - the A600 config has a megabyte of fast RAM - but
 * had no coverage at all, because the harness answered "no card" everywhere.
 */
#include "protocol.h"

/* er_Type: bits 7-6 = %11 marks Zorro II, bits 2-0 are the size code. */
#define ER_ZORRO_II     0xC0
#define ER_SIZE_64K     1
#define ER_SIZE_128K    2
#define ER_SIZE_1M      5
#define ER_SIZE_8M      0

/* er_Flags bit 7 set means a memory board. */
#define ERF_MEMORY      0x80
#define ERF_IO          0x00

#define FAST_BASE       0x200000u

static uint32_t configure(void)
{
    h_result r;
    h_begin_call();
    r = h_call(h_sym("configure_zorro_ii"));
    CHECK_CALL(r);
    return h_get_d(0);
}

static void t_no_card(void)
{
    /* Nothing attached: the bus reads $FF and there is no first RAM base. */
    CHECK_U32(0u, configure());
    CHECK_CONTAINS("No card found", h_serial());
}

static void t_memory_card_relocated(void)
{
    h_attach_zorro(ER_ZORRO_II | ER_SIZE_1M, ERF_MEMORY);

    CHECK_U32(FAST_BASE, configure());
    CHECK(h_zorro_configured(), "card should have been relocated");
    CHECK_U32(FAST_BASE, h_zorro_base());
    CHECK_CONTAINS("Memory card found", h_serial());
}

static void t_memory_card_base_nibbles(void)
{
    /* The base address goes out as four nibbles in the high half of four
     * byte writes, and the one at $48 is the trigger. Getting the order
     * wrong puts the card somewhere else entirely. */
    h_attach_zorro(ER_ZORRO_II | ER_SIZE_1M, ERF_MEMORY);
    configure();
    CHECK_U32(0x00200000u, h_zorro_base());
}

static void t_io_card_shut_up(void)
{
    h_attach_zorro(ER_ZORRO_II | ER_SIZE_64K, ERF_IO);

    /* An I/O card contributes no RAM, and must be silenced so the next scan
     * does not keep finding it. */
    CHECK_U32(0u, configure());
    CHECK(h_zorro_shut_up(), "I/O card should have been shut up");
    CHECK_CONTAINS("I/O card found", h_serial());
}

static void t_not_zorro_ii_ignored(void)
{
    /* Bits 7-6 other than %11 are not a Zorro II card. */
    h_attach_zorro(0x40 | ER_SIZE_1M, ERF_MEMORY);
    CHECK_U32(0u, configure());
    CHECK_CONTAINS("No card found", h_serial());
}

static void t_scan_terminates_with_card_present(void)
{
    /* The card stops answering once relocated, so the scan must end rather
     * than configure the same card eight times. The empty slot that ends it
     * reports itself, which is where the "No card found" after a successful
     * card on a normal boot comes from - easy to drop by accident when the
     * loop is restructured. */
    h_attach_zorro(ER_ZORRO_II | ER_SIZE_1M, ERF_MEMORY);
    configure();
    CHECK_CONTAINS("Memory card found", h_serial());
    CHECK_CONTAINS("No card found", h_serial());
    CHECK_CONTAINS("Autoconfig: Done", h_serial());
}

static void t_size_code_advances_next_base(void)
{
    /* A 128KB board must leave the next allocation at $220000, not $300000.
     * The 8MB special case for code 0 is the trap here. */
    h_attach_zorro(ER_ZORRO_II | ER_SIZE_128K, ERF_MEMORY);
    CHECK_U32(FAST_BASE, configure());
    CHECK_U32(FAST_BASE, h_zorro_base());
}

static const test_case tests[] = {
    { "no_card",             t_no_card,                       NULL },
    { "memory_card",         t_memory_card_relocated,         NULL },
    { "base_nibbles",        t_memory_card_base_nibbles,      NULL },
    { "io_card_shut_up",     t_io_card_shut_up,               NULL },
    { "not_zorro_ii",        t_not_zorro_ii_ignored,          NULL },
    { "scan_terminates",     t_scan_terminates_with_card_present, NULL },
    { "size_code_128k",      t_size_code_advances_next_base,  NULL },
};

const test_suite zorro_suite = { "zorro", tests, sizeof tests / sizeof tests[0] };
