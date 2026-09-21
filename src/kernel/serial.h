/*
 * serial.h - kernel serial output
 *
 * Two modes, one API. Until ser_irq_enable() the driver polls, so early boot
 * output is on the wire when the call returns. After it, bytes go through a
 * ring buffer drained by the level 1 TBE interrupt and ser_putc returns
 * without waiting for 9600 baud. See docs/serial_design.md.
 *
 * Every function here is safe in any context: interrupts enabled or masked,
 * task or ISR.
 */

#ifndef SERIAL_H
#define SERIAL_H

/* Ring sizes. Powers of two, so wrap is a mask. Transmit holds about a
 * second at 9600; receive holds more than anyone types ahead. */
#define SER_RING_SIZE     1024
#define SER_RX_RING_SIZE  256

/*
 * Set the baud rate and start in polled mode.
 * Call this even though ROM sets it up - ensures known state.
 */
void ser_init(void);

/*
 * Switch to interrupt-driven transmit. ser_tbe_isr must already be attached
 * to IRQ_TBE - irq_init() does both. Safe to call again at any time:
 * it restarts a transmitter whose interrupt was lost.
 */
void ser_irq_enable(void);

/*
 * Output len bytes. Polled mode: blocks until the UART has taken them.
 * Interrupt mode: queues them and returns. Each chunk of up to 256 bytes
 * enters the ring as a unit, so concurrent writers never interleave inside
 * one - a kprintf line stays a line.
 *
 * If the ring has no room: a task that can sleep does, until the ring has
 * drained to half. A caller that cannot - a handler, anyone with interrupts
 * already masked, anything before the scheduler starts - moves queued bytes
 * to the UART itself until there is room, so it can never deadlock waiting
 * for an interrupt that is masked.
 */
long ser_write(const void *buf, unsigned long len);

/* ser_write of one character. */
void ser_putc(char c);

/*
 * Output null-terminated string.
 */
void ser_puts(const char *s);

/*
 * Block until everything queued is on the wire: ring empty and the shift
 * register drained. Polls, with interrupts masked for the duration, so it
 * works when interrupts do not - call it before handing the UART to anyone
 * else, the ROM debugger above all.
 */
void ser_flush(void);

/* Bytes queued and not yet handed to the UART. */
unsigned long ser_tx_pending(void);

/* The TBE interrupt handler, attached to IRQ_TBE by irq_init(). Runs with
 * all interrupts masked; not for anyone else. */
void ser_tbe_isr(void *arg);

/*
 * Receive. Interrupt-driven once ser_irq_enable() has run: the RBF handler
 * fills a ring and ser_read() empties it.
 *
 * ser_read blocks until at least one byte is available, then returns as
 * many as are waiting, up to len. Task context only - it sleeps.
 */
long          ser_read(void *buf, unsigned long len);
unsigned long ser_rx_ready(void);
void          ser_rbf_isr(void *arg);

/* Bytes lost, and why: the ring was full because nobody was reading, or
 * Paula's one-byte buffer was overwritten because the handler was kept
 * waiting - some critical section ran longer than a character time. */
extern volatile unsigned long ser_rx_total;      /* every byte received */
extern volatile unsigned long ser_rx_dropped;
extern volatile unsigned long ser_rx_overruns;

/*
 * Check if receive buffer has data.
 */
int ser_can_read(void);

/*
 * Read character, polling. Only for before interrupts are up; after that
 * the RBF handler takes every byte and this sees nothing. Use ser_read.
 */
char ser_getc(void);

#endif /* SERIAL_H */
