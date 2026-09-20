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

/* Ring size. A power of two, so wrap is a mask. About a second at 9600. */
#define SER_RING_SIZE 1024

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
 * Output single character. Polled mode: blocks until the UART takes it.
 * Interrupt mode: queues and returns; if the ring is full, moves queued
 * bytes to the UART itself until there is room, so it can never deadlock
 * waiting for an interrupt that is masked.
 */
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
 * Check if receive buffer has data.
 */
int ser_can_read(void);

/*
 * Read character. Blocks until data available. Receive is polled: in normal
 * operation the only reader of the serial line is the ROM debugger.
 */
char ser_getc(void);

#endif /* SERIAL_H */
