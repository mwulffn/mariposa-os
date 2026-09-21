/*
 * cia.c - CIA-A's interrupt control register, and its one owner
 */
#include "cia.h"
#include "irq.h"
#include "cpu.h"
#include "amiga_hw.h"

static struct {
    void (*handler)(void *);
    void  *arg;
} sources[CIA_NSOURCES];

static void ciaa_isr(void *arg)
{
    unsigned char flags;
    unsigned int i;

    (void)arg;

    /* One read. It returns what is pending and clears it, which also drops
     * the CIA's interrupt line - so PORTS can be cleared in Paula now and
     * will stay clear. In the other order it would not: the line is a
     * level, and Paula sets the bit straight back. Anything the CIA raises
     * from here on is a new request and re-enters after this returns. */
    flags = ciaa.icr;
    custom.intreq = INTF_PORTS;

    for (i = 0; i < CIA_NSOURCES; i++)
        if ((flags & (1U << i)) && sources[i].handler)
            sources[i].handler(sources[i].arg);
}

void cia_init(void)
{
    unsigned int i;

    for (i = 0; i < CIA_NSOURCES; i++)
        sources[i].handler = 0;

    ciaa.icr = 0x7F;                    /* bit 7 clear: mask all five */
    (void)ciaa.icr;                     /* and forget whatever was pending */

    irq_attach(IRQ_PORTS, ciaa_isr, 0);
    irq_enable(IRQ_PORTS);
}

void ciaa_attach(unsigned int source, void (*handler)(void *), void *arg)
{
    if (source >= CIA_NSOURCES)
        return;
    CRITICAL_ENTER();
    sources[source].handler = handler;
    sources[source].arg     = arg;
    CRITICAL_EXIT();
}

void ciaa_enable(unsigned int source)
{
    if (source < CIA_NSOURCES)
        ciaa.icr = (unsigned char)(CIAICRF_SETCLR | (1U << source));
}

void ciaa_disable(unsigned int source)
{
    if (source < CIA_NSOURCES)
        ciaa.icr = (unsigned char)(1U << source);
}

void ciaa_timer_a_oneshot(unsigned short ticks)
{
    CRITICAL_ENTER();
    /* Stop, select one-shot, then load: in one-shot mode writing the high
     * byte of a stopped timer loads the latch into the counter and starts
     * it. SPMODE lives in the same register and is the keyboard's, so the
     * other bits are preserved. */
    ciaa.cra = (unsigned char)((ciaa.cra & CIACRAF_SPMODE) | CIACRAF_RUNMODE);
    ciaa.talo = (unsigned char)(ticks & 0xFF);
    ciaa.tahi = (unsigned char)(ticks >> 8);
    CRITICAL_EXIT();
}
