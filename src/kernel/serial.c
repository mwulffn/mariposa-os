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
#include "irq.h"
#include "task.h"
#include "chardev.h"

#define RING_MASK (SER_RING_SIZE - 1)

static unsigned char ring[SER_RING_SIZE];
static volatile unsigned short head;        /* next free slot: producers */
static volatile unsigned short tail;        /* next byte to send: consumers */
static unsigned char           irq_mode;    /* ser_irq_enable has run */

/* Everything below that touches the ring runs with interrupts
 * masked: inside CRITICAL_ENTER, or in the ISR, which switch.s enters with
 * everything masked for the same reason. */

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

void ser_irq_enable(void)
{
    CRITICAL_ENTER();
    irq_mode = 1;
    irq_enable(IRQ_TBE);
    irq_enable(IRQ_RBF);

    /* Bytes may already be queued behind an interrupt that is not coming:
     * irq_init clears INTREQ wholesale, and so can anyone else. Raising TBE
     * by hand runs the ISR, which looks at the UART and carries on from
     * wherever it really is. With nothing queued it is one wasted entry. */
    if (!ring_empty())
        custom.intreq = INTF_SETCLR | INTF_TBE;
    CRITICAL_EXIT();
}

/* Queued bytes, and the most one ser_write() chunk may be: a writer waits
 * for room for its whole chunk, so a chunk has to be able to fit. */
#define RING_USED()  ((unsigned short)((head - tail) & RING_MASK))
#define RING_FREE()  ((unsigned short)(RING_MASK - RING_USED()))
#define CHUNK_MAX    256
#define LOW_WATER    (SER_RING_SIZE / 2)    /* sleepers wake below this */

static struct waitq tx_wait;

/*
 * Write a chunk as one unit: every byte goes into the ring inside a single
 * critical section, so two writers cannot interleave within it. That is
 * what keeps a kprintf line whole, and it is why kprintf no longer has to
 * hold interrupts off for as long as the line takes to format and send.
 *
 * The question is what to do when the ring has no room, which with a task
 * that prints faster than 9600 baud is nearly always.
 *
 * A task that may sleep, sleeps: on tx_wait, until the TBE handler has
 * drained the ring to the low-water mark. The alternative - what this
 * replaced - is to move bytes to the UART by polling with interrupts
 * masked, which holds the whole machine at wire speed for the length of a
 * line: lower priority tasks starve, ticks are lost, and received bytes are
 * overrun in Paula's one-byte buffer.
 *
 * Everyone else still polls, because everyone else cannot sleep: a handler,
 * the kernel before the scheduler starts, and - the subtle one - any caller
 * that arrived with interrupts already masked. Sleeping would switch tasks
 * in the middle of that caller's critical section and quietly hand its
 * half-updated state to someone else.
 */
static void write_chunk(const unsigned char *p, unsigned short len)
{
    unsigned long sr = cpu_int_disable();
    int may_sleep = (sr & 0x0700) == 0 && sched_can_block();
    unsigned short i;

    while (RING_FREE() < len) {
        if (may_sleep)
            task_wait(&tx_wait);    /* comes back masked, as it left */
        else
            tx_pump_wait();
    }

    for (i = 0; i < len; i++) {
        ring[head] = p[i];
        head = (head + 1) & RING_MASK;
    }

    /* Idle transmitter: this starts it. Busy: this does nothing, and the
     * byte in flight raises the TBE that collects these. With the CPU
     * masked it is also the only thing keeping output moving. */
    tx_pump();
    cpu_sr_set(sr);
}

long ser_write(const void *buf, unsigned long len)
{
    const unsigned char *p = buf;
    unsigned long left = len;

    if (!irq_mode) {
        for (; left; left--, p++) {
            while (!serial_hw_tx_ready())
                ;
            serial_hw_tx(*p);
        }
        return (long)len;
    }

    while (left) {
        unsigned short n = left > CHUNK_MAX ? CHUNK_MAX : (unsigned short)left;

        write_chunk(p, n);
        p += n;
        left -= n;
    }
    return (long)len;
}

void ser_putc(char c)
{
    ser_write(&c, 1);
}

void ser_puts(const char *s)
{
    while (*s)
        ser_putc(*s++);
}

void ser_tbe_isr(void *arg)
{
    (void)arg;

    /* Acknowledge before looking, so a byte that frees the buffer between
     * the two is a fresh request rather than a lost one. */
    custom.intreq = INTF_TBE;

    /* Empty: go quiet. The next write restarts things. Not ready: a stale
     * request, and the real one is still on its way. Both are tx_pump()
     * doing nothing. */
    tx_pump();

    /* Writers asleep on a full ring are woken at the low-water mark, not at
     * the first free byte: a task that wakes per character has gained
     * nothing over polling. All of them, since each wants a different
     * amount and re-checks for itself. */
    if (tx_wait.head && RING_USED() <= LOW_WATER)
        wake_all(&tx_wait);
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

/* ------------------------------------------------------------- receive --- */

/*
 * The mirror image of transmit: the RBF interrupt (level 5) is the producer
 * and tasks are the consumers. Paula buffers exactly one received byte, so
 * the handler's whole job is to get it out before the next one lands - at
 * 9600 baud that is about a millisecond, and a critical section longer than
 * that anywhere in the kernel costs input. When it happens it is counted,
 * because "input went missing" and "input was never sent" are different
 * bugs and look identical from the far end of the cable.
 */
#define RX_MASK (SER_RX_RING_SIZE - 1)

static unsigned char rx_ring[SER_RX_RING_SIZE];
static volatile unsigned short rx_head;     /* the handler's */
static volatile unsigned short rx_tail;     /* the readers' */
static struct waitq rx_wait;

volatile unsigned long ser_rx_total;        /* bytes taken from the UART */
volatile unsigned long ser_rx_dropped;      /* ring full: nobody reading */
volatile unsigned long ser_rx_overruns;     /* Paula's buffer overwritten */

void ser_rbf_isr(void *arg)
{
    unsigned short next;
    unsigned char c;

    (void)arg;
    if (serial_hw_rx_overrun())
        ser_rx_overruns++;
    c = serial_hw_rx();                     /* takes the byte and acks RBF */
    ser_rx_total++;

    next = (rx_head + 1) & RX_MASK;
    if (next == rx_tail) {
        /* Drop the newest: what was typed first is what survives. */
        ser_rx_dropped++;
        return;
    }
    rx_ring[rx_head] = c;
    rx_head = next;
    wake_one(&rx_wait);
}

unsigned long ser_rx_ready(void)
{
    unsigned long n;

    CRITICAL_ENTER();
    n = (unsigned long)((rx_head - rx_tail) & RX_MASK);
    CRITICAL_EXIT();
    return n;
}

long ser_read(void *buf, unsigned long len)
{
    unsigned char *out = buf;
    long n = 0;

    if (len == 0)
        return 0;

    CRITICAL_ENTER();
    /* Masked from the emptiness test to the wait, so a byte cannot arrive
     * in between and leave this task asleep on a ring that is not empty.
     * A loop, not an if: another reader may have got there first. */
    while (rx_head == rx_tail)
        task_wait(&rx_wait);
    while ((unsigned long)n < len && rx_head != rx_tail) {
        out[n++] = rx_ring[rx_tail];
        rx_tail = (rx_tail + 1) & RX_MASK;
    }
    CRITICAL_EXIT();
    return n;
}

/* -------------------------------------------------------------- chardev --- */

static long ser0_read(struct device *dev, void *buf, unsigned long len)
{
    (void)dev;
    return ser_read(buf, len);
}

static long ser0_write(struct device *dev, const void *buf, unsigned long len)
{
    (void)dev;
    return ser_write(buf, len);
}

static unsigned long ser0_rx_ready(struct device *dev)
{
    (void)dev;
    return ser_rx_ready();
}

static const struct chardev_ops ser0_ops = {
    ser0_read, ser0_write, ser0_rx_ready
};

static struct device ser0 = { "ser0", DEV_CHAR, &ser0_ops, 0, 0 };

/* ----------------------------------------------------- polled receive --- */

/* For before interrupts are up. Once they are, the RBF handler takes every
 * byte and these two see nothing: use ser_read. */
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

/* ----------------------------------------------------------------- init --- */

void ser_init(void)
{
    /* The ROM has already done this; make the state explicit anyway. */
    serial_hw_init(SERIAL_BAUD_9600);
    head = tail = 0;
    rx_head = rx_tail = 0;
    rx_wait.head = rx_wait.tail = 0;
    tx_wait.head = tx_wait.tail = 0;
    ser_rx_total = ser_rx_dropped = ser_rx_overruns = 0;
    irq_mode = 0;
    dev_register(&ser0);        /* refused, harmlessly, on a second init */
}
