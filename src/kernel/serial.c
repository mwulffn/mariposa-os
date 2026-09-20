/*
 * serial.c - kernel serial output
 *
 * This is the half of serial that is NOT shared with the ROM, and
 * deliberately so: the kernel buffers and lets the level 1 TBE interrupt do
 * the waiting, while the ROM must keep polling because its debugger has to
 * work on a machine whose kernel has already died. Everything below the
 * waiting strategy lives in src/shared/serial_hw.c. See
 * docs/serial_design.md.
 *
 * The transmitter is restarted by software, never by enabling an interrupt
 * and hoping. TBE is raised by a byte leaving the buffer and by nothing
 * else: once the ISR has acknowledged the last one and found the ring empty
 * there is no request left to fire, and enabling TBE in INTENA at that point
 * waits for ever. So INTENA's TBE bit stays on, and every ser_putc offers the
 * UART a byte itself: on an idle transmitter that starts it, on a busy one
 * it does nothing and the byte in flight raises the TBE that carries on.
 *
 * Nothing here trusts the interrupt to mean "the buffer is free". The ROM
 * polls the same UART, INTREQ bits can be set by software, and a request can
 * be left over from either. tx_pump() asks the UART, every time.
 */

#include "serial.h"
#include "serial_hw.h"
#include "amiga_hw.h"
#include "cpu.h"

#define RING_MASK (SER_RING_SIZE - 1)

static unsigned char ring[SER_RING_SIZE];
static volatile unsigned short head;        /* next free slot: producers */
static volatile unsigned short tail;        /* next byte to send: consumers */
static unsigned char           irq_mode;    /* ser_irq_enable has run */

/* Everything below that touches the ring runs with interrupts
 * masked: inside CRITICAL_ENTER, or in the ISR, which vectors.s enters at
 * level 7 for the same reason. */

static int ring_empty(void) { return head == tail; }
static int ring_full(void)  { return ((head + 1) & RING_MASK) == tail; }

/* Hand the UART the next queued byte if, and only if, it can take one. */
static void tx_pump(void)
{
    if (ring_empty() || !serial_hw_tx_ready())
        return;
    serial_hw_tx(ring[tail]);
    tail = (tail + 1) & RING_MASK;
}

/* Move one queued byte to the UART by polling. Needs a non-empty ring. */
static void tx_pump_wait(void)
{
    while (!serial_hw_tx_ready())
        ;
    tx_pump();
}

void ser_init(void)
{
    /* The ROM has already done this; make the state explicit anyway. */
    serial_hw_init(SERIAL_BAUD_9600);
    head = tail = 0;
    irq_mode = 0;
}

void ser_irq_enable(void)
{
    CRITICAL_ENTER();
    irq_mode = 1;
    custom.intena = INTF_SETCLR | INTF_TBE;

    /* Bytes may already be queued behind an interrupt that is not coming:
     * irq_init clears INTREQ wholesale, and so can anyone else. Raising TBE
     * by hand runs the ISR, which looks at the UART and carries on from
     * wherever it really is. With nothing queued it is one wasted entry. */
    if (!ring_empty())
        custom.intreq = INTF_SETCLR | INTF_TBE;
    CRITICAL_EXIT();
}

void ser_putc(char c)
{
    if (!irq_mode) {
        while (!serial_hw_tx_ready())
            ;
        serial_hw_tx((unsigned char)c);
        return;
    }

    CRITICAL_ENTER();

    /* Full. The design doc says spin until the ISR makes room, which never
     * returns if the caller has interrupts masked - kprintf in a critical
     * section, or in an ISR. Do the ISR's job from here instead: order is
     * kept because it is the same ring and the same end of it. */
    while (ring_full())
        tx_pump_wait();

    ring[head] = (unsigned char)c;
    head = (head + 1) & RING_MASK;

    /* Idle transmitter: this starts it. Busy: this does nothing, and the
     * byte in flight raises the TBE that collects this one. With the CPU
     * masked it is also the only thing keeping output moving. */
    tx_pump();
    CRITICAL_EXIT();
}

void ser_puts(const char *s)
{
    while (*s)
        ser_putc(*s++);
}

void ser_tbe_isr(void)
{
    /* Acknowledge before looking, so a byte that frees the buffer between
     * the two is a fresh request rather than a lost one. */
    custom.intreq = INTF_TBE;

    /* Empty: go quiet. The next ser_putc restarts things. Not ready: a
     * stale request, and the real one is still on its way. Both are
     * tx_pump() doing nothing. */
    tx_pump();
}

void ser_flush(void)
{
    CRITICAL_ENTER();
    while (!ring_empty())
        tx_pump_wait();
    while (!serial_hw_tx_drained())
        ;
    CRITICAL_EXIT();
}

unsigned long ser_tx_pending(void)
{
    unsigned long n;

    CRITICAL_ENTER();
    n = (unsigned long)((head - tail) & RING_MASK);
    CRITICAL_EXIT();
    return n;
}

int ser_can_read(void)
{
    return serial_hw_rx_ready();
}

char ser_getc(void)
{
    while (!serial_hw_rx_ready())
        ;
    return (char)serial_hw_rx();
}
