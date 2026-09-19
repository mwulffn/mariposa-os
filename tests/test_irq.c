/*
 * test_irq.c - Paula's interrupt registers and 68000 autovector dispatch.
 *
 * These exist because the `isr` branch concluded that FS-UAE "doesn't
 * correctly read exception vectors from chip RAM during interrupt processing
 * on 68000/68010" and moved the whole project to a 68020 with VBR=0 to work
 * around it. Nothing here needs an emulator: this is the real Musashi 68000
 * core dispatching through a real vector table in chip RAM. If autovector
 * dispatch were broken on 68000, these would fail.
 *
 * The three bugs that actually explained the symptom are covered next door in
 * test_vectors.c (vector install order, group 0 panic frames).
 */
#include "protocol.h"

#include <string.h>

/* --- hand-assembled guest stubs -----------------------------------------
 *
 * Tiny routines built byte by byte rather than assembled, so these tests need
 * no extra build step. Each returns the guest address it was loaded at.
 */

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/*
 * An interrupt handler:
 *      addq.l  #1,counter
 *      move.w  #ack,$DFF09C        ; omitted when ack == 0
 *      rte
 */
static uint32_t stub_handler(uint32_t counter, uint16_t ack)
{
    uint8_t code[16];
    size_t n = 0;

    put16(code + n, 0x52B9); n += 2;          /* addq.l #1,<abs.l> */
    put32(code + n, counter); n += 4;

    if (ack) {
        put16(code + n, 0x33FC); n += 2;      /* move.w #imm,<abs.l> */
        put16(code + n, ack);    n += 2;
        put32(code + n, 0x00DFF09Cu); n += 4; /* INTREQ */
    }

    put16(code + n, 0x4E73); n += 2;          /* rte */
    return h_alloc(code, n);
}

/*
 * Foreground code for the interrupt to land in: four NOPs then RTS.
 */
static uint32_t stub_nops(void)
{
    uint8_t code[10];
    size_t n = 0;
    int i;

    for (i = 0; i < 4; i++) { put16(code + n, 0x4E71); n += 2; }
    put16(code + n, 0x4E75); n += 2;          /* rts */
    return h_alloc(code, n);
}

/*
 * Guest code that writes the two interrupt registers itself:
 *      move.w  #ena,$DFF09A
 *      move.w  #req,$DFF09C
 *      rts
 */
static uint32_t stub_write_regs(uint16_t ena, uint16_t req)
{
    uint8_t code[20];
    size_t n = 0;

    put16(code + n, 0x33FC); n += 2;
    put16(code + n, ena);    n += 2;
    put32(code + n, 0x00DFF09Au); n += 4;

    put16(code + n, 0x33FC); n += 2;
    put16(code + n, req);    n += 2;
    put32(code + n, 0x00DFF09Cu); n += 4;

    put16(code + n, 0x4E75); n += 2;
    return h_alloc(code, n);
}

/* A longword of scratch for a handler to count in. */
static uint32_t counter_cell(void)
{
    uint8_t zero[4] = { 0, 0, 0, 0 };
    return h_alloc(zero, sizeof zero);
}

/* --- registers ---------------------------------------------------------- */

static void t_intena_setclr(void)
{
    /* Bit 15 set means "set these bits", clear means "clear these bits". */
    h_write_intena(H_INTF_SETCLR | H_INTF_INTEN | H_INTF_VERTB);
    CHECK_U32(H_INTF_INTEN | H_INTF_VERTB, h_intena());

    h_write_intena(H_INTF_SETCLR | H_INTF_PORTS);
    CHECK_U32(H_INTF_INTEN | H_INTF_VERTB | H_INTF_PORTS, h_intena());

    h_write_intena(H_INTF_VERTB);                  /* clear VERTB only */
    CHECK_U32(H_INTF_INTEN | H_INTF_PORTS, h_intena());

    /* What bootstrap.s does at reset: clear everything, master bit included. */
    h_write_intena(0x7FFF);
    CHECK_U32(0, h_intena());
}

static void t_intreq_setclr(void)
{
    h_raise(H_INTF_VERTB | H_INTF_TBE);
    CHECK_U32(H_INTF_VERTB | H_INTF_TBE, h_intreq());

    h_write_intreq(H_INTF_VERTB);
    CHECK_U32(H_INTF_TBE, h_intreq());

    h_write_intreq(0x7FFF);
    CHECK_U32(0, h_intreq());
}

/*
 * Read the two status ports the way guest code does:
 *      move.w  $DFF01C,d0      ; INTENAR
 *      move.w  $DFF01E,d1      ; INTREQR
 *      rts
 * h_peek16 would not do: it reads the memory model directly and never reaches
 * the custom-register decode.
 */
static uint32_t stub_read_regs(void)
{
    uint8_t code[16];
    size_t n = 0;

    put16(code + n, 0x3039); n += 2;          /* move.w <abs.l>,d0 */
    put32(code + n, 0x00DFF01Cu); n += 4;
    put16(code + n, 0x3239); n += 2;          /* move.w <abs.l>,d1 */
    put32(code + n, 0x00DFF01Eu); n += 4;
    put16(code + n, 0x4E75); n += 2;
    return h_alloc(code, n);
}

static void t_registers_read_back(void)
{
    uint32_t pc = stub_read_regs();
    h_result r;

    h_write_intena(H_INTF_SETCLR | H_INTF_INTEN | H_INTF_RBF);
    h_raise(H_INTF_EXTER);

    h_begin_call();
    r = h_call(pc);
    CHECK_CALL(r);

    CHECK_U32(H_INTF_INTEN | H_INTF_RBF, h_get_d(0) & 0xFFFFu);  /* INTENAR */
    CHECK_U32(H_INTF_EXTER,              h_get_d(1) & 0xFFFFu);  /* INTREQR */
}

static void t_guest_writes_registers(void)
{
    /* The same path a real driver takes: move.w to $DFF09A / $DFF09C. */
    uint32_t pc = stub_write_regs(H_INTF_SETCLR | H_INTF_INTEN | H_INTF_VERTB,
                                  H_INTF_SETCLR | H_INTF_VERTB);
    h_result r;

    h_begin_call();
    r = h_call(pc);
    CHECK_CALL(r);

    CHECK_U32(H_INTF_INTEN | H_INTF_VERTB, h_intena());
    CHECK_U32(H_INTF_VERTB, h_intreq());
    CHECK_U32(3, h_irq_level());
}

/* --- level computation --------------------------------------------------- */

static void t_master_enable_gates(void)
{
    /* Requested and individually enabled, but no master bit: nothing. */
    h_write_intena(H_INTF_SETCLR | H_INTF_VERTB);
    h_raise(H_INTF_VERTB);
    CHECK_U32(0, h_irq_level());

    h_write_intena(H_INTF_SETCLR | H_INTF_INTEN);
    CHECK_U32(3, h_irq_level());
}

static void t_level_mapping(void)
{
    static const struct { uint16_t bit; int level; } map[] = {
        { H_INTB_TBE,    1 }, { H_INTB_DSKBLK, 1 }, { H_INTB_SOFTINT, 1 },
        { H_INTB_PORTS,  2 },
        { H_INTB_COPER,  3 }, { H_INTB_VERTB,  3 }, { H_INTB_BLIT,    3 },
        { H_INTB_AUD0,   4 }, { H_INTB_AUD3,   4 },
        { H_INTB_RBF,    5 }, { H_INTB_DSKSYN, 5 },
        { H_INTB_EXTER,  6 },
    };
    size_t i;

    for (i = 0; i < sizeof map / sizeof map[0] && !t_failed(); i++) {
        uint16_t f = H_INTF(map[i].bit);

        h_write_intena(0x7FFF);
        h_write_intreq(0x7FFF);
        h_write_intena(H_INTF_SETCLR | H_INTF_INTEN | f);
        h_raise(f);

        CHECK(h_irq_level() == map[i].level,
              "INTREQ bit %u should raise level %d, got %d",
              (unsigned)map[i].bit, map[i].level, h_irq_level());
    }
}

static void t_highest_level_wins(void)
{
    h_write_intena(H_INTF_SETCLR | H_INTF_INTEN |
                   H_INTF_VERTB | H_INTF_EXTER | H_INTF_TBE);
    h_raise(H_INTF_VERTB | H_INTF_TBE);
    CHECK_U32(3, h_irq_level());

    h_raise(H_INTF_EXTER);                         /* level 6 now pending */
    CHECK_U32(6, h_irq_level());

    h_write_intreq(H_INTF_EXTER);                  /* ack it */
    CHECK_U32(3, h_irq_level());
}

static void t_disabled_source_is_invisible(void)
{
    /* Requested but not enabled contributes nothing, and stays pending. */
    h_write_intena(H_INTF_SETCLR | H_INTF_INTEN | H_INTF_VERTB);
    h_raise(H_INTF_VERTB | H_INTF_EXTER);
    CHECK_U32(3, h_irq_level());
    CHECK_U32(H_INTF_VERTB | H_INTF_EXTER, h_intreq());

    h_write_intena(H_INTF_SETCLR | H_INTF_EXTER);  /* enable it late */
    CHECK_U32(6, h_irq_level());
}

/* --- dispatch on a real 68000 -------------------------------------------- */

static void t_autovector_dispatch(void)
{
    /* The claim this suite exists to check: a vector written to chip RAM is
     * honoured when the interrupt fires, on a 68000, with no VBR anywhere. */
    uint32_t counter = counter_cell();
    uint32_t handler = stub_handler(counter, H_INTF_VERTB);
    uint32_t body    = stub_nops();
    h_result r;

    h_poke32(0x06C, handler);                      /* autovector 3 */

    h_write_intena(H_INTF_SETCLR | H_INTF_INTEN | H_INTF_VERTB);
    h_raise(H_INTF_VERTB);
    CHECK_U32(3, h_irq_level());

    h_begin_call();
    h_set_sr(0x2000);                              /* supervisor, IPL 0 */
    r = h_call(body);

    CHECK_CALL(r);
    CHECK_U32(1, h_peek32(counter));               /* handler ran, exactly once */
    CHECK_U32(0, h_intreq());                      /* and acked itself */
    CHECK_U32(0, h_irq_level());
}

static void t_masked_by_sr(void)
{
    /* SR=$2700 is what bootstrap.s hands the kernel. Nothing gets through. */
    uint32_t counter = counter_cell();
    uint32_t handler = stub_handler(counter, H_INTF_VERTB);
    uint32_t body    = stub_nops();
    h_result r;

    h_poke32(0x06C, handler);
    h_write_intena(H_INTF_SETCLR | H_INTF_INTEN | H_INTF_VERTB);
    h_raise(H_INTF_VERTB);

    h_begin_call();
    h_set_sr(0x2700);
    r = h_call(body);
    CHECK_CALL(r);
    CHECK_U32(0, h_peek32(counter));
    CHECK_U32(H_INTF_VERTB, h_intreq());           /* still pending */

    /* Level 3 is below the mask at IPL 3 too - it has to be strictly above. */
    h_begin_call();
    h_set_sr(0x2300);
    r = h_call(body);
    CHECK_CALL(r);
    CHECK_U32(0, h_peek32(counter));

    h_begin_call();
    h_set_sr(0x2200);
    r = h_call(body);
    CHECK_CALL(r);
    CHECK_U32(1, h_peek32(counter));
}

static void t_unacked_interrupt_reenters(void)
{
    /* A handler that forgets to clear its INTREQ bit is re-entered the moment
     * it RTEs. Modelling this rather than papering over it is the point: it
     * is a real and easy bug to write. */
    uint32_t counter = counter_cell();
    uint32_t handler = stub_handler(counter, 0);   /* no ack */
    uint32_t body    = stub_nops();
    h_result r;

    h_poke32(0x06C, handler);
    h_write_intena(H_INTF_SETCLR | H_INTF_INTEN | H_INTF_VERTB);
    h_raise(H_INTF_VERTB);

    h_set_cycle_budget(20000);
    h_begin_call();
    h_set_sr(0x2000);
    r = h_call(body);

    CHECK(r.status == H_TIMEOUT, "expected a runaway, got %s", r.detail);
    CHECK(h_peek32(counter) > 1,
          "handler should have re-entered, ran %u time(s)",
          (unsigned)h_peek32(counter));
}

static void t_level7_is_non_maskable(void)
{
    /* Level 7 does not come from Paula and ignores the SR mask. */
    uint32_t counter = counter_cell();
    uint32_t handler = stub_handler(counter, 0);
    uint32_t body    = stub_nops();
    h_result r;

    h_poke32(0x07C, handler);                      /* autovector 7 */
    h_irq_force(7);
    CHECK_U32(7, h_irq_level());

    h_set_cycle_budget(20000);
    h_begin_call();
    h_set_sr(0x2700);                              /* masked, but 7 gets in */
    r = h_call(body);

    CHECK(h_peek32(counter) > 0, "level 7 should fire through the SR mask");
    (void)r;
}

/* --- the serial transmitter ----------------------------------------------
 *
 * The transmitter takes time and raises TBE when its buffer frees. Both were
 * missing from the model, which made the interrupt-driven transmit path in
 * docs/serial_design.md impossible to test: a UART that is permanently ready
 * never spins a polling loop and never raises an interrupt.
 */

/*
 *      move.w  #$0141,$DFF030      ; send 'A' (bit 8 is the stop bit)
 *      move.w  $DFF018,d1          ; status immediately after
 *      moveq   #60,d0
 *      dbf     d0,*                ; burn time
 *      move.w  $DFF018,d2          ; status once it has drained
 *      rts
 */
static uint32_t stub_send_and_sample(void)
{
    uint8_t code[24];
    size_t n = 0;

    put16(code + n, 0x33FC); n += 2;          /* move.w #imm,<abs.l> */
    put16(code + n, 0x0141); n += 2;
    put32(code + n, 0x00DFF030u); n += 4;     /* SERDAT */

    put16(code + n, 0x3239); n += 2;          /* move.w <abs.l>,d1 */
    put32(code + n, 0x00DFF018u); n += 4;     /* SERDATR */

    put16(code + n, 0x7000 | 60); n += 2;     /* moveq #60,d0 */
    put16(code + n, 0x51C8); n += 2;          /* dbf d0,... */
    put16(code + n, 0xFFFE); n += 2;          /* ... to itself */

    put16(code + n, 0x3439); n += 2;          /* move.w <abs.l>,d2 */
    put32(code + n, 0x00DFF018u); n += 4;

    put16(code + n, 0x4E75); n += 2;
    return h_alloc(code, n);
}

#define SERDATF_TBE_  0x2000u
#define SERDATF_TSRE_ 0x1000u

static void t_tx_takes_time(void)
{
    uint32_t pc = stub_send_and_sample();
    h_result r;
    uint32_t busy, idle;

    h_begin_call();
    r = h_call(pc);
    CHECK_CALL(r);

    busy = h_get_d(1);
    idle = h_get_d(2);

    CHECK(!(busy & SERDATF_TBE_),  "TBE should be clear right after SERDAT");
    CHECK(!(busy & SERDATF_TSRE_), "TSRE should be clear right after SERDAT");
    CHECK(idle & SERDATF_TBE_,  "TBE should be set once the buffer frees");
    CHECK(idle & SERDATF_TSRE_, "TSRE should be set once the shifter empties");

    CHECK_STR("A", h_serial());
}

/*
 *      move.w  #$0141,$DFF030      ; send
 *      moveq   #60,d0
 *      dbf     d0,*
 *      rts
 */
static uint32_t stub_send_then_spin(void)
{
    uint8_t code[16];
    size_t n = 0;

    put16(code + n, 0x33FC); n += 2;
    put16(code + n, 0x0141); n += 2;
    put32(code + n, 0x00DFF030u); n += 4;
    put16(code + n, 0x7000 | 60); n += 2;
    put16(code + n, 0x51C8); n += 2;
    put16(code + n, 0xFFFE); n += 2;
    put16(code + n, 0x4E75); n += 2;
    return h_alloc(code, n);
}

static void t_tbe_interrupt_fires(void)
{
    /* This is the mechanism the kernel's ring buffer will be built on: send a
     * byte, and the level 1 TBE interrupt asks for the next one. */
    uint32_t counter = counter_cell();
    uint32_t handler = stub_handler(counter, H_INTF_TBE);
    uint32_t body    = stub_send_then_spin();
    h_result r;

    h_poke32(0x064, handler);                      /* autovector 1 */
    h_write_intena(H_INTF_SETCLR | H_INTF_INTEN | H_INTF_TBE);

    h_begin_call();
    h_set_sr(0x2000);
    r = h_call(body);

    CHECK_CALL(r);
    CHECK_U32(1, h_peek32(counter));               /* fired once */
    CHECK_U32(0, h_intreq());                      /* and the handler acked */
}

static void t_tbe_silent_until_transmit(void)
{
    /* An idle transmitter must not raise TBE on its own, or every test that
     * enables level 1 would take a spurious interrupt. */
    uint32_t counter = counter_cell();
    uint32_t handler = stub_handler(counter, H_INTF_TBE);
    uint32_t body    = stub_nops();
    h_result r;

    h_poke32(0x064, handler);
    h_write_intena(H_INTF_SETCLR | H_INTF_INTEN | H_INTF_TBE);

    h_begin_call();
    h_set_sr(0x2000);
    r = h_call(body);

    CHECK_CALL(r);
    CHECK_U32(0, h_peek32(counter));
}

/* --- the ROM's own handler ----------------------------------------------- */

static void t_rom_autovector_reaches_panic(void)
{
    /* End to end against the real ROM: install_exception_vectors, then fire a
     * VERTB. It should land in auto_vec_handler and panic, which is the ROM
     * behaving correctly - nothing enables interrupts yet, so any interrupt
     * really is unexpected. Before 2210e79 this reported "UNKNOWN EXCEPTION"
     * instead, because the generic fill had eaten the autovectors.
     */
    uint32_t body = stub_nops();
    h_result r;

    h_begin_call();
    r = h_call(h_sym("install_exception_vectors"));
    CHECK_CALL(r);
    CHECK_U32(h_sym("auto_vec_handler"), h_peek32(0x06C));

    h_write_intena(H_INTF_SETCLR | H_INTF_INTEN | H_INTF_VERTB);
    h_raise(H_INTF_VERTB);

    h_serial_clear();
    h_set_cycle_budget(4000000);
    h_begin_call();
    h_set_sr(0x2000);
    h_run(body);

    CHECK_CONTAINS("AUTOVECTOR INTERRUPT", h_serial());
}

/* ------------------------------------------------------------------------ */

static const test_case tests[] = {
    { "intena_setclr",          t_intena_setclr,             NULL },
    { "intreq_setclr",          t_intreq_setclr,             NULL },
    { "registers_read_back",    t_registers_read_back,       NULL },
    { "guest_writes_registers", t_guest_writes_registers,    NULL },
    { "master_enable_gates",    t_master_enable_gates,       NULL },
    { "level_mapping",          t_level_mapping,             NULL },
    { "highest_level_wins",     t_highest_level_wins,        NULL },
    { "disabled_source_hidden", t_disabled_source_is_invisible, NULL },
    { "autovector_dispatch",    t_autovector_dispatch,       NULL },
    { "masked_by_sr",           t_masked_by_sr,              NULL },
    { "unacked_reenters",       t_unacked_interrupt_reenters, NULL },
    { "level7_non_maskable",    t_level7_is_non_maskable,    NULL },
    { "rom_autovector_panics",  t_rom_autovector_reaches_panic, NULL },
    { "tx_takes_time",          t_tx_takes_time,             NULL },
    { "tbe_interrupt_fires",    t_tbe_interrupt_fires,       NULL },
    { "tbe_silent_until_tx",    t_tbe_silent_until_transmit, NULL },
};

const test_suite irq_suite = { "irq", tests, sizeof tests / sizeof tests[0] };
