/*
 * test_cpu.c - src/kernel/cpu.s and src/kernel/isr.s
 *
 * The first interrupt code in the project, and the reason the irq.* suite
 * was built before it: interrupts can be got wrong in ways that look like
 * an emulator bug, and were, on the branch this replaces. Both files are
 * position independent, so they are assembled standalone to origin zero and
 * loaded like libsup.s.
 *
 * cpu.h's prototypes all use unsigned long, so every argument is one
 * longword slot and h_push32 matches what vbcc would push.
 */
#include "protocol.h"

#include <stdint.h>

/* --- SR primitives ------------------------------------------------------ */

static uint32_t call0(const char *name)
{
    h_result r;
    h_begin_call();
    r = h_call(h_sym(name));
    CHECK_CALL(r);
    return h_get_d(0);
}

static void call1(const char *name, uint32_t arg)
{
    h_result r;
    h_begin_call();
    h_push32(arg);
    r = h_call(h_sym(name));
    CHECK_CALL(r);
}

static void t_sr_get_reads_sr(void)
{
    h_set_sr(0x2700);
    CHECK_U32(0x2700u, call0("_cpu_sr_get"));
}

/* Zero extended: a caller comparing against an unsigned long must not have
 * to care that SR is 16 bits. */
static void t_sr_get_is_zero_extended(void)
{
    h_set_d(0, 0xFFFFFFFFu);
    h_set_sr(0x2000);
    CHECK_U32(0x2000u, call0("_cpu_sr_get"));
}

static void t_sr_set_writes_sr(void)
{
    h_set_sr(0x2700);
    call1("_cpu_sr_set", 0x2000);
    CHECK_U32(0x2000u, h_get_sr());
}

static void t_int_disable_masks_everything(void)
{
    h_set_sr(0x2000);
    call0("_cpu_int_disable");
    CHECK_U32(0x2700u, h_get_sr());
}

/*
 * The half the design doc does not spell out but rule 1 requires: a caller
 * cannot restore what it was never told, so disable hands back the old SR.
 * Checked at an intermediate level, not just 0 and 7, so that a version
 * returning a constant could not pass.
 */
static void t_int_disable_returns_previous_sr(void)
{
    h_set_sr(0x2400);
    CHECK_U32(0x2400u, call0("_cpu_int_disable"));
    CHECK_U32(0x2700u, h_get_sr());
}

static void t_int_enable_unmasks_everything(void)
{
    h_set_sr(0x2700);
    call0("_cpu_int_enable");
    CHECK_U32(0x2000u, h_get_sr());
}

/* Supervisor is never given up: bit 13 stays set through all of these. */
static void t_primitives_stay_in_supervisor(void)
{
    h_set_sr(0x2700);
    call0("_cpu_int_enable");
    CHECK((h_get_sr() & 0x2000u) != 0,
          "cpu_int_enable left SR $%X - supervisor bit dropped", h_get_sr());
}

/* --- the vertical blank handler ------------------------------------------
 *
 * Driven the way irq.autovector_dispatch drives its synthetic stub, but
 * against the real handler: raise VERTB with the master enable on, run a
 * body of NOPs at SR $2000, and see what the interrupt did to it.
 */

/* NOP NOP NOP NOP RTS - long enough to be interrupted, returns to the
 * harness sentinel when it is not. */
static uint32_t nop_body(void)
{
    static const uint8_t code[] = {
        0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x75
    };
    return h_alloc(code, sizeof code);
}

static void arm_vbl(void)
{
    h_poke32(0x06C, h_sym("_vbl_handler"));     /* level 3 autovector */
    h_poke32(h_sym("_vbl_count"), 0);
    h_write_intena(H_INTF_SETCLR | H_INTF_INTEN | H_INTF_VERTB);
}

static void t_vbl_handler_counts(void)
{
    h_result r;

    arm_vbl();
    h_raise(H_INTF_VERTB);
    CHECK_U32(3, h_irq_level());

    h_begin_call();
    h_set_sr(0x2000);
    r = h_call(nop_body());

    CHECK_CALL(r);
    CHECK_U32(1u, h_peek32(h_sym("_vbl_count")));
}

/*
 * The ack is the whole difference between a handler and a lockup. INTREQ
 * bit 5 stays set until it is written back and the level stays asserted,
 * so a handler that forgets is re-entered the instant it RTEs - which is
 * why the count being exactly 1 is the assertion that matters here.
 */
static void t_vbl_handler_acks(void)
{
    h_result r;

    arm_vbl();
    h_raise(H_INTF_VERTB);

    h_begin_call();
    h_set_sr(0x2000);
    r = h_call(nop_body());

    CHECK_CALL(r);
    CHECK_U32(1u, h_peek32(h_sym("_vbl_count")));   /* not re-entered */
    CHECK_U32(0u, h_intreq());                      /* bit cleared */
    CHECK_U32(0, h_irq_level());                    /* level dropped */
}

/* Only VERTB, and only VERTB's bit. A handler acking with $FFFF would take
 * every other pending source down with it. */
static void t_vbl_handler_leaves_other_sources_pending(void)
{
    h_result r;

    arm_vbl();
    h_raise(H_INTF_VERTB | H_INTF_PORTS);

    h_begin_call();
    h_set_sr(0x2000);
    r = h_call(nop_body());
    CHECK_CALL(r);

    CHECK_U32(H_INTF_PORTS, h_intreq());
}

/* An ISR that returns with a register changed corrupts whatever it
 * interrupted, and does it somewhere else entirely. */
static void t_vbl_handler_preserves_registers(void)
{
    h_result r;

    arm_vbl();
    h_raise(H_INTF_VERTB);

    h_begin_call();
    h_set_a(0, 0x00123456u);
    h_set_d(0, 0x89ABCDEFu);
    h_set_sr(0x2000);
    r = h_call(nop_body());
    CHECK_CALL(r);

    CHECK_U32(0x00123456u, h_get_a(0));
    CHECK_U32(0x89ABCDEFu, h_get_d(0));
}

/* Masked at the CPU, nothing arrives however loudly Paula asks - which is
 * exactly the state bootstrap.s hands the kernel, and why irq_init alone
 * is not enough. */
static void t_vbl_handler_silent_while_sr_masks(void)
{
    h_result r;

    arm_vbl();
    h_raise(H_INTF_VERTB);

    h_begin_call();
    h_set_sr(0x2700);
    r = h_call(nop_body());
    CHECK_CALL(r);

    CHECK_U32(0u, h_peek32(h_sym("_vbl_count")));
    CHECK_U32(H_INTF_VERTB, h_intreq());        /* still pending */
}

/* --- idle -----------------------------------------------------------------
 *
 * cpu_idle is STOP #$2000: it lowers the mask and halts in one instruction,
 * and comes back after the handler has run.
 */
static void t_idle_wakes_on_interrupt(void)
{
    h_result r;

    arm_vbl();
    h_raise(H_INTF_VERTB);

    h_begin_call();
    h_set_sr(0x2700);                   /* masked: STOP itself must unmask */
    r = h_call(h_sym("_cpu_idle"));

    CHECK_CALL(r);
    CHECK_U32(1u, h_peek32(h_sym("_vbl_count")));
    CHECK_U32(0x2000u, h_get_sr() & 0xFF00u);
}

/* With nothing to wake it, it must actually stop rather than fall through:
 * an idle loop built on a cpu_idle that returns at once is a spin loop with
 * extra steps. */
static void t_idle_really_stops(void)
{
    h_result r;

    h_begin_call();
    h_set_cycle_budget(100000);
    r = h_call(h_sym("_cpu_idle"));

    CHECK(r.status == H_TIMEOUT,
          "cpu_idle returned with no interrupt pending (status %d)",
          (int)r.status);
}

/* ------------------------------------------------------------------------ */

static const test_case tests[] = {
    { "sr_get",                t_sr_get_reads_sr,                 NULL },
    { "sr_get_zero_extended",  t_sr_get_is_zero_extended,         NULL },
    { "sr_set",                t_sr_set_writes_sr,                NULL },
    { "int_disable",           t_int_disable_masks_everything,    NULL },
    { "int_disable_returns",   t_int_disable_returns_previous_sr, NULL },
    { "int_enable",            t_int_enable_unmasks_everything,   NULL },
    { "stays_supervisor",      t_primitives_stay_in_supervisor,   NULL },
    { "vbl_counts",            t_vbl_handler_counts,              NULL },
    { "vbl_acks",              t_vbl_handler_acks,                NULL },
    { "vbl_acks_only_vertb",   t_vbl_handler_leaves_other_sources_pending, NULL },
    { "vbl_preserves_regs",    t_vbl_handler_preserves_registers, NULL },
    { "vbl_masked_by_sr",      t_vbl_handler_silent_while_sr_masks, NULL },
    { "idle_wakes",            t_idle_wakes_on_interrupt,         NULL },
    { "idle_really_stops",     t_idle_really_stops,               NULL },
};

const test_suite cpu_suite = { "cpu", tests, sizeof tests / sizeof tests[0] };
