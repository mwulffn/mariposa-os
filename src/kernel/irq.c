/*
 * irq.c - interrupt sources, shared levels and their dispatch
 *
 * One assembly entry per CPU level (switch.s) masks interrupts and calls
 * irq_dispatch(level), which runs the handler of every source on that level
 * that is both enabled and pending, and leaves through isr_exit - so every
 * handler gets the scheduler's switch-on-exit without knowing about it.
 */
#include "amiga_hw.h"
#include "irq.h"
#include "serial.h"
#include "task.h"
#include "vector.h"
#include "cpu.h"
#include "input.h"

extern void irq_level1(void), irq_level2(void), irq_level3(void);
extern void irq_level4(void), irq_level5(void), irq_level6(void);
extern void yield_handler(void);

static struct {
    void (*handler)(void *);
    void  *arg;
} sources[IRQ_NSOURCES];

/* Which sources Paula puts on which level. Hardware, not policy. */
static const unsigned short level_sources[7] = {
    0,
    INTF_TBE | INTF_DSKBLK | INTF_SOFTINT,
    INTF_PORTS,
    INTF_COPER | INTF_VERTB | INTF_BLIT,
    INTF_AUD0 | INTF_AUD1 | INTF_AUD2 | INTF_AUD3,
    INTF_RBF | INTF_DSKSYNC,
    INTF_EXTER
};

volatile unsigned long irq_spurious;

void irq_attach(unsigned int source, void (*handler)(void *), void *arg)
{
    if (source >= IRQ_NSOURCES)
        return;
    CRITICAL_ENTER();
    sources[source].handler = handler;
    sources[source].arg     = arg;
    CRITICAL_EXIT();
}

void irq_enable(unsigned int source)
{
    if (source < IRQ_NSOURCES)
        custom.intena = INTF_SETCLR | (1U << source);
}

void irq_disable(unsigned int source)
{
    if (source < IRQ_NSOURCES)
        custom.intena = (unsigned short)(1U << source);
}

/* Called by switch.s with every interrupt masked. */
void irq_dispatch(unsigned long level)
{
    /* Enabled AND pending. INTREQ bits are set by the hardware whether or
     * not anyone asked, so pending alone would run handlers for sources
     * their drivers had switched off. */
    unsigned short active = custom.intreqr & custom.intenar &
                            level_sources[level];
    unsigned int source;

    for (source = 0; active; source++, active >>= 1) {
        if (!(active & 1))
            continue;
        if (sources[source].handler) {
            sources[source].handler(sources[source].arg);
        } else {
            /* Nobody will acknowledge this, so it would re-enter for ever.
             * Switch it off, clear it, and leave a count behind. */
            custom.intena = (unsigned short)(1U << source);
            custom.intreq = (unsigned short)(1U << source);
            irq_spurious++;
        }
    }
}

/* Vertical blank, 50Hz: the scheduler's tick. */
static void tick_isr(void *arg)
{
    (void)arg;
    custom.intreq = INTF_VERTB;
    vbl_count++;
    sched_tick();
    input_tick();               /* key repeat */
}

void irq_init(void)
{
    unsigned int i;

    /* Before anything can interrupt: from here on a crash flushes the
     * serial ring ahead of the ROM's panic dump. */
    trap_init(vector_table());

    /* The ROM filled the table with handlers that panic, which was the
     * right default while nothing was dispatched. Now every level is, and
     * an unexpected source is counted and shut off instead. */
    vector_set(VEC_AUTOVECTOR(1), irq_level1);
    vector_set(VEC_AUTOVECTOR(2), irq_level2);
    vector_set(VEC_AUTOVECTOR(3), irq_level3);
    vector_set(VEC_AUTOVECTOR(4), irq_level4);
    vector_set(VEC_AUTOVECTOR(5), irq_level5);
    vector_set(VEC_AUTOVECTOR(6), irq_level6);
    vector_set(VEC_TRAP(0), yield_handler);

    for (i = 0; i < IRQ_NSOURCES; i++)
        sources[i].handler = 0;
    irq_spurious = 0;

    /*
     * Disable and clear everything before enabling anything, or a request
     * left over from the ROM's boot would fire the moment the gate opens.
     * Bit 15 clear is the CLR direction, so $7FFF clears all fourteen.
     */
    custom.intena = 0x7FFF;
    custom.intreq = 0x7FFF;

    irq_attach(IRQ_VERTB, tick_isr, 0);
    irq_attach(IRQ_TBE, ser_tbe_isr, 0);
    irq_attach(IRQ_RBF, ser_rbf_isr, 0);

    /* Open Paula's gate. Nothing arrives yet - SR still masks it - until
     * the caller runs cpu_int_enable(). */
    custom.intena = INTF_SETCLR | INTF_INTEN;
    irq_enable(IRQ_VERTB);

    /* Last, because it has to come after the INTREQ clear above: that may
     * have thrown away the TBE request a queued byte was waiting on, and
     * ser_irq_enable restarts the transmitter rather than trusting it. */
    ser_irq_enable();
}
