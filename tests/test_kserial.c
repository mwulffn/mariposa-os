/*
 * test_kserial.c - src/kernel/serial.c, the interrupt-driven transmit path
 *
 * Run against the real kernel image: main.c loads SYSTEM.BIN at $200000 and
 * its symbols under "kernel:". Nothing here is a copy built for the tests.
 *
 * What the driver promises, and so what is pinned here:
 *   - before ser_irq_enable it is polled, so early boot output is on the
 *     wire when the call returns and an early crash loses nothing;
 *   - after it, ser_putc returns without waiting for the wire;
 *   - bytes come out complete and in order in every context, including with
 *     interrupts masked and the ring full, where waiting for the ISR would
 *     be a deadlock;
 *   - SERDAT is never written while TBE is clear (h_serial_overruns);
 *   - a crash gets the queued output out before the ROM's panic dump.
 *
 * vbcc pushes every argument as a longword, right to left.
 */
#include "protocol.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define K(name) h_sym("kernel:" name)

/* Slow enough that code outruns the wire by a wide margin, fast enough that
 * a few thousand characters stay well inside the cycle budget. */
#define SLOW_TBE   2000
#define SLOW_TSRE  4000

static void kcall0(const char *name)
{
    h_result r;
    h_begin_call();
    r = h_call(h_sym(name));
    CHECK_CALL(r);
}

static void kcall1(const char *name, uint32_t arg)
{
    h_result r;
    h_begin_call();
    h_push32(arg);
    r = h_call(h_sym(name));
    CHECK_CALL(r);
}

/* A recognisable stream: position is recoverable from content, so a dropped,
 * duplicated or reordered byte shows up as a mismatch at a known offset. */
static void fill_pattern(char *buf, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
        buf[i] = (char)('!' + (i * 7 + i / 90) % 90);
    buf[n] = '\0';
}

static void check_stream(const char *want, size_t n)
{
    const char *got = h_serial();
    size_t i;

    CHECK_U32(n, h_serial_len());
    for (i = 0; i < n && i < h_serial_len(); i++) {
        if (got[i] != want[i]) {
            t_fail("stream differs at byte %u of %u: want '%c' got '%c'",
                   (unsigned)i, (unsigned)n, want[i], got[i]);
            return;
        }
    }
    CHECK_U32(0, h_serial_overruns());
}

/*      moveq   #n,d1
 * .o:  move.w  #$FFFF,d0
 * .i:  dbf     d0,.i
 *      dbf     d1,.o
 *      rts
 * About 650k cycles a lap: time for interrupts to do their work while the
 * foreground does nothing at all. */
static uint32_t spin_body(int laps)
{
    uint8_t code[] = {
        0x72, 0x00,                 /* moveq #laps-1,d1 */
        0x30, 0x3C, 0xFF, 0xFF,     /* move.w #$FFFF,d0 */
        0x51, 0xC8, 0xFF, 0xFE,     /* dbf d0,* */
        0x51, 0xC9, 0xFF, 0xF6,     /* dbf d1,.o */
        0x4E, 0x75
    };
    code[1] = (uint8_t)(laps - 1);
    return h_alloc(code, sizeof code);
}

static void spin(int laps)
{
    h_result r;
    h_begin_call();
    r = h_call(spin_body(laps));
    CHECK_CALL(r);
}

/* What kernel_main does: polled serial, then the interrupt wiring. */
static void boot_to_irq_mode(void)
{
    kcall0("kernel:_ser_init");
    kcall0("kernel:_irq_init");
}

/* --- polled, before interrupts ------------------------------------------ */

static void t_polled_until_enabled(void)
{
    kcall0("kernel:_ser_init");
    h_serial_set_timing(SLOW_TBE, SLOW_TSRE);

    kcall1("kernel:_ser_puts", h_str("early boot"));

    /* All of it, already, with the CPU masked the whole time. */
    CHECK_STR("early boot", h_serial());
    CHECK_U32(0, h_serial_overruns());
    CHECK_U32(0, h_intena() & H_INTF_TBE);
}

/* --- interrupt driven ---------------------------------------------------- */

static void t_putc_does_not_wait_for_the_wire(void)
{
    /* 9600 baud for real: about 7400 cycles a character. */
    const uint64_t per_char = 7400;
    char text[101];
    h_result r;

    fill_pattern(text, 100);
    boot_to_irq_mode();
    h_serial_set_timing(per_char, per_char * 2);

    h_begin_call();
    h_push32(h_str(text));
    h_set_sr(0x2000);
    r = h_call(K("_ser_puts"));
    CHECK_CALL(r);

    /* Polled, this cannot return in under 99 character times. Queued, it
     * costs what the code costs, and the wire is somebody else's problem. */
    CHECK(r.cycles < 25 * per_char,
          "ser_puts blocked: 100 bytes took %lu cycles, %lu character times",
          (unsigned long)r.cycles, (unsigned long)(r.cycles / per_char));
    CHECK(h_serial_len() < 100, "everything was already sent on return");

    /* The foreground does nothing; the TBE interrupt does the rest. */
    h_set_sr(0x2000);
    spin(2);
    check_stream(text, 100);
}

static void t_goes_quiet_when_drained(void)
{
    boot_to_irq_mode();
    h_set_sr(0x2000);
    kcall1("kernel:_ser_puts", h_str("abc"));
    h_set_sr(0x2000);
    spin(1);

    CHECK_STR("abc", h_serial());
    CHECK_U32(0, h_irq_level());            /* not left asserting level 1 */
    CHECK_U32(0, h_intreq() & H_INTF_TBE);
}

/*
 * The trap in the obvious design: the ISR acks TBE, finds the ring empty and
 * stops. Nothing will ever raise TBE again, because only a transmission
 * does - so the next ser_putc has to restart the transmitter itself rather
 * than enable an interrupt that is never coming.
 */
static void t_restarts_from_idle(void)
{
    boot_to_irq_mode();
    h_set_sr(0x2000);
    kcall1("kernel:_ser_puts", h_str("one "));
    h_set_sr(0x2000);
    spin(1);
    kcall1("kernel:_ser_puts", h_str("two "));
    h_set_sr(0x2000);
    spin(1);
    kcall1("kernel:_ser_puts", h_str("three"));
    h_set_sr(0x2000);
    spin(1);

    CHECK_STR("one two three", h_serial());
    CHECK_U32(0, h_serial_overruns());
}

/* Three rings' worth through a slow UART: the ring fills, and the producer
 * has to be held back without losing or reordering anything. */
static void t_more_than_the_ring_holds(void)
{
    static char text[3001];

    fill_pattern(text, 3000);
    boot_to_irq_mode();
    h_serial_set_timing(SLOW_TBE, SLOW_TSRE);

    h_set_sr(0x2000);
    kcall1("kernel:_ser_puts", h_str(text));
    kcall0("kernel:_ser_flush");
    check_stream(text, 3000);
}

/*
 * The same with the CPU masked, which is where "spin until the ISR makes
 * room" - the design doc's answer to a full ring - never returns. kprintf
 * from inside a critical section or an ISR is exactly this.
 */
static void t_full_ring_with_interrupts_masked(void)
{
    static char text[3001];

    fill_pattern(text, 3000);
    boot_to_irq_mode();
    h_serial_set_timing(SLOW_TBE, SLOW_TSRE);

    h_set_sr(0x2700);
    kcall1("kernel:_ser_puts", h_str(text));
    kcall0("kernel:_ser_flush");
    check_stream(text, 3000);
    CHECK_U32(0x2700, h_get_sr() & 0xFF00); /* and it stayed masked */
}

/* flush means on the wire: shifter empty, not merely out of the ring. */
static void t_flush_waits_for_the_shifter(void)
{
    boot_to_irq_mode();
    h_serial_set_timing(SLOW_TBE, SLOW_TSRE);
    h_set_sr(0x2000);
    kcall1("kernel:_ser_puts", h_str("goodbye"));
    h_set_sr(0x2000);
    kcall0("kernel:_ser_flush");
    CHECK_U32(0x2000, h_get_sr() & 0xFF00); /* caller's SR restored */

    CHECK_STR("goodbye", h_serial());
    kcall0("kernel:_serial_hw_tx_drained");
    CHECK(h_get_d(0) != 0, "TSRE clear after ser_flush");
}

/*
 * A TBE request that does not mean "the buffer is free": left over from the
 * ROM's polled output, or set by software. The ISR must look at the UART,
 * not trust the interrupt, or it writes SERDAT over a byte in flight.
 */
static void t_stale_tbe_does_not_overrun(void)
{
    boot_to_irq_mode();
    h_serial_set_timing(50000, 60000);      /* a byte is in flight for ages */

    h_set_sr(0x2700);
    kcall1("kernel:_ser_puts", h_str("xyz"));
    h_raise(H_INTF_TBE);                    /* lie */
    h_set_sr(0x2000);
    spin(1);

    CHECK_STR("xyz", h_serial());
    CHECK_U32(0, h_serial_overruns());
}

/*
 * irq_init clears INTREQ wholesale, and output queued while the CPU was
 * masked may be waiting on exactly the TBE it throws away. Enabling has to
 * restart the transmitter rather than hope.
 */
static void t_enable_restarts_a_stalled_queue(void)
{
    boot_to_irq_mode();
    h_serial_set_timing(SLOW_TBE, SLOW_TSRE);
    h_set_sr(0x2700);
    kcall1("kernel:_ser_puts", h_str("queued while masked"));
    spin(1);                                /* first byte gone, TBE raised... */
    CHECK(h_intreq() & H_INTF_TBE, "test setup: no TBE pending");
    h_write_intreq(0x7FFF);                 /* ...and someone clears everything */
    kcall0("kernel:_ser_irq_enable");
    h_set_sr(0x2000);
    spin(1);

    CHECK_STR("queued while masked", h_serial());
    CHECK_U32(0, h_serial_overruns());
}

/* The handler calls C, and C is free with D0/D1/A0/A1. */
static void t_isr_preserves_registers(void)
{
    static const uint8_t nops[] = {
        0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71,
        0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x75
    };
    h_result r;
    int i;

    boot_to_irq_mode();
    h_set_sr(0x2700);
    kcall1("kernel:_ser_puts", h_str("ab"));
    spin(1);                                /* masked: 'a' sent, TBE pending */
    CHECK(h_intreq() & H_INTF_TBE, "test setup: no TBE pending");

    h_begin_call();
    for (i = 0; i < 8; i++) h_set_d(i, 0xD0D0D000u + (uint32_t)i);
    for (i = 0; i < 7; i++) h_set_a(i, 0x00A0A000u + (uint32_t)i * 4);
    h_set_sr(0x2000);
    r = h_call(h_alloc(nops, sizeof nops));
    CHECK_CALL(r);

    CHECK_STR("ab", h_serial());            /* the ISR did run */
    for (i = 0; i < 8; i++) CHECK_U32(0xD0D0D000u + (uint32_t)i, h_get_d(i));
    for (i = 0; i < 7; i++) CHECK_U32(0x00A0A000u + (uint32_t)i * 4, h_get_a(i));
}

/* --- crash path ----------------------------------------------------------
 *
 * The ROM's panic bangs the UART and knows nothing about the ring. Without
 * help, a crash prints the register dump and silently drops up to a second
 * of the output that led to it - the lines that matter most.
 */
static void t_crash_flushes_the_ring_first(void)
{
    static const uint8_t illegal[] = { 0x4A, 0xFC };    /* ILLEGAL */
    uint32_t pc = h_alloc(illegal, sizeof illegal);
    const char *out, *said, *dump;
    char want[32];
    h_result r;

    kcall0("install_exception_vectors");    /* the ROM's, as at boot */
    boot_to_irq_mode();
    h_serial_set_timing(SLOW_TBE, SLOW_TSRE);

    h_set_sr(0x2700);
    kcall1("kernel:_ser_puts", h_str("last words before the crash\r\n"));

    h_begin_call();
    h_set_cycle_budget(20000000);
    r = h_run(pc);
    (void)r;                                /* ends in the debugger: timeout */

    out  = h_serial();
    said = strstr(out, "last words before the crash");
    dump = strstr(out, "ILLEGAL INSTRUCTION");
    CHECK(said != NULL, "queued output was lost in the crash");
    CHECK(dump != NULL, "ROM panic did not run");
    if (said && dump)
        CHECK(said < dump, "panic dump overtook the queued output");

    /* And the ROM still decodes the frame it was handed. */
    snprintf(want, sizeof want, "PC:$%08X", (unsigned)pc);
    CHECK_CONTAINS(want, out);
    CHECK_U32(0, h_serial_overruns());
}

/* ------------------------------------------------------------------------ */

static const test_case tests[] = {
    { "polled_until_enabled",   t_polled_until_enabled,              NULL },
    { "putc_does_not_block",    t_putc_does_not_wait_for_the_wire,   NULL },
    { "quiet_when_drained",     t_goes_quiet_when_drained,           NULL },
    { "restarts_from_idle",     t_restarts_from_idle,                NULL },
    { "ring_overflow_in_order", t_more_than_the_ring_holds,          NULL },
    { "full_ring_masked",       t_full_ring_with_interrupts_masked,  NULL },
    { "flush_waits_for_tsre",   t_flush_waits_for_the_shifter,       NULL },
    { "stale_tbe_no_overrun",   t_stale_tbe_does_not_overrun,        NULL },
    { "enable_restarts_queue",  t_enable_restarts_a_stalled_queue,   NULL },
    { "isr_preserves_regs",     t_isr_preserves_registers,           NULL },
    { "crash_flushes_ring",     t_crash_flushes_the_ring_first,      NULL },
};

const test_suite kserial_suite = { "kser", tests, sizeof tests / sizeof tests[0] };
