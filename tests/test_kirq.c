/*
 * test_kirq.c - src/kernel/irq.c: shared interrupt levels, dispatched
 *
 * Paula folds fourteen sources onto six CPU levels, so one handler per
 * vector cannot work: level 1 alone carries serial transmit, disk block and
 * the software interrupt. These pin irq_attach and the per-level dispatch
 * against the real kernel image, with handlers from guest/ktasks.s.
 */
#include "protocol.h"

#include <stdint.h>
#include <string.h>

#define SRC_TBE      0u     /* level 1 */
#define SRC_DSKBLK   1u     /* level 1 */
#define SRC_SOFTINT  2u     /* level 1 */
#define SRC_BLIT     6u     /* level 3 */

#define BIT(n) ((uint16_t)(1u << (n)))

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

static void setup(void)
{
    uint32_t cpu = 0;
    kcall("kernel:_ser_init", 0, NULL);
    kcall("kernel:_sched_init", 1, &cpu);
    kcall("kernel:_irq_init", 0, NULL);
}

/* A counter block for isr_count_ack: +0 count, +8 INTREQ bits to clear. */
static uint32_t attach(uint32_t source, uint16_t ack)
{
    uint8_t zero[16] = {0};
    uint32_t a[3];

    a[0] = source;
    a[1] = h_sym("isr_count_ack");
    a[2] = h_alloc(zero, sizeof zero);
    h_poke32(a[2] + 8, ack);
    kcall("kernel:_irq_attach", 3, a);
    return a[2];
}

static void enable(uint32_t source)  { kcall("kernel:_irq_enable", 1, &source); }
static void disable(uint32_t source) { kcall("kernel:_irq_disable", 1, &source); }

/* Unmasked foreground for interrupts to land in. */
static void let_it_run(void)
{
    static const uint8_t nops[] = {
        0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71,
        0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x75
    };
    h_result r;

    h_begin_call();
    h_set_sr(0x2000);
    h_set_cycle_budget(200000);
    r = h_call(h_alloc(nops, sizeof nops));
    CHECK_CALL(r);
}

static void t_two_sources_one_level(void)
{
    uint32_t a, b;

    setup();
    a = attach(SRC_DSKBLK,  BIT(SRC_DSKBLK));
    b = attach(SRC_SOFTINT, BIT(SRC_SOFTINT));
    enable(SRC_DSKBLK);
    enable(SRC_SOFTINT);

    h_raise(BIT(SRC_DSKBLK) | BIT(SRC_SOFTINT));
    let_it_run();

    CHECK_U32(1, h_peek32(a));
    CHECK_U32(1, h_peek32(b));
    CHECK_U32(0, h_intreq());
}

/* Pending is not enough. INTREQ bits are set by the hardware whether or not
 * anyone asked, and a handler must not be run for a source it disabled. */
static void t_only_enabled_sources_run(void)
{
    uint32_t a, b;

    setup();
    a = attach(SRC_DSKBLK,  BIT(SRC_DSKBLK));
    b = attach(SRC_SOFTINT, BIT(SRC_SOFTINT));
    enable(SRC_DSKBLK);

    h_raise(BIT(SRC_DSKBLK) | BIT(SRC_SOFTINT));
    let_it_run();

    CHECK_U32(1, h_peek32(a));
    CHECK_U32(0, h_peek32(b));
    CHECK_U32(BIT(SRC_SOFTINT), h_intreq());        /* still waiting */
}

static void t_disable_stops_delivery(void)
{
    uint32_t a;

    setup();
    a = attach(SRC_SOFTINT, BIT(SRC_SOFTINT));
    enable(SRC_SOFTINT);
    h_raise(BIT(SRC_SOFTINT));
    let_it_run();
    CHECK_U32(1, h_peek32(a));

    disable(SRC_SOFTINT);
    h_raise(BIT(SRC_SOFTINT));
    let_it_run();
    CHECK_U32(1, h_peek32(a));
    CHECK_U32(0, h_intena() & BIT(SRC_SOFTINT));
}

/*
 * An enabled source nobody handles would re-enter for ever: nothing
 * acknowledges it, so the level never drops. The dispatcher has to end it,
 * and say so, without taking down the sources that are fine.
 */
static void t_unhandled_source_is_shut_off(void)
{
    uint32_t good;

    setup();
    good = attach(SRC_DSKBLK, BIT(SRC_DSKBLK));
    enable(SRC_DSKBLK);
    h_write_intena(H_INTF_SETCLR | BIT(SRC_BLIT));  /* behind irq.c's back */

    h_raise(BIT(SRC_BLIT));
    let_it_run();                                   /* returns at all */

    CHECK_U32(0, h_intena() & BIT(SRC_BLIT));
    CHECK_U32(1, h_peek32(h_sym("kernel:_irq_spurious")));

    h_raise(BIT(SRC_DSKBLK));
    let_it_run();
    CHECK_U32(1, h_peek32(good));
}

/* A handler that forgets to acknowledge is the same storm from the other
 * side. The dispatcher cannot know the device, so it cannot fix it - but it
 * must not be the one that hides it: the source stays pending, visibly. */
static void t_unacked_handler_reenters(void)
{
    uint32_t a;
    h_result r;
    static const uint8_t nops[] = { 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x75 };

    setup();
    a = attach(SRC_SOFTINT, 0);                     /* acknowledges nothing */
    enable(SRC_SOFTINT);
    h_raise(BIT(SRC_SOFTINT));

    h_begin_call();
    h_set_sr(0x2000);
    h_set_cycle_budget(100000);
    r = h_call(h_alloc(nops, sizeof nops));

    CHECK(r.status == H_TIMEOUT, "expected an interrupt storm, got status %d",
          (int)r.status);
    CHECK(h_peek32(a) > 10, "handler ran %u times", h_peek32(a));
}

static const test_case tests[] = {
    { "two_sources_one_level",  t_two_sources_one_level,        NULL },
    { "only_enabled_run",       t_only_enabled_sources_run,     NULL },
    { "disable_stops_delivery", t_disable_stops_delivery,       NULL },
    { "unhandled_shut_off",     t_unhandled_source_is_shut_off, NULL },
    { "unacked_reenters",       t_unacked_handler_reenters,     NULL },
};

const test_suite kirq_suite = { "kirq", tests, sizeof tests / sizeof tests[0] };
