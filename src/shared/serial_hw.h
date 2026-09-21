/*
 * serial_hw.h - Paula UART register primitives, shared by ROM and kernel
 *
 * This layer is deliberately thin and deliberately has no policy. It knows
 * how to ask the UART whether it is ready and how to hand it a byte; it does
 * not know how to wait.
 *
 * That boundary is the whole point. The ROM polls - it has no interrupts and
 * must keep working when a crashed kernel has left the machine in an unknown
 * state, so the debugger bangs the UART directly. The kernel is meant to go
 * the other way: a ring buffer drained by the level 1 TBE interrupt, so
 * kprintf is not held hostage to 9600 baud (docs/serial_design.md).
 *
 * Those two strategies must not be shared, or the kernel inherits a spin
 * loop and the ROM inherits a dependency on interrupts working. What they
 * can share is everything below the strategy, which is this file.
 *
 * Nothing here blocks, allocates, or keeps state.
 */
#ifndef SERIAL_HW_H
#define SERIAL_HW_H

/* SERPER divisors: 3546895 / baud - 1 (PAL colour clock). */
#define SERIAL_BAUD_9600    368
#define SERIAL_BAUD_19200   184
#define SERIAL_BAUD_38400    91

/* Set the baud rate divisor. Does not touch interrupt enables. */
void serial_hw_init(unsigned short serper);

/* Transmit buffer empty - the UART will accept a byte now. */
int serial_hw_tx_ready(void);

/* Transmit shift register empty - the previous byte is fully on the wire.
 * Stricter than tx_ready and much slower; the ROM's crash paths use it so
 * output is not lost if the machine stops immediately afterwards. */
int serial_hw_tx_drained(void);

/* Hand a byte to the UART. Caller must have checked serial_hw_tx_ready();
 * writing when it is not ready loses the byte in flight. */
void serial_hw_tx(unsigned char c);

/* Receive buffer full - a byte is waiting. */
int serial_hw_rx_ready(void);

/* A byte was lost: another arrived before the last was acknowledged. Ask
 * BEFORE serial_hw_rx(), whose acknowledgement clears it. */
int serial_hw_rx_overrun(void);

/* Take the pending byte AND acknowledge it by clearing INTREQ's RBF bit.
 * Reading SERDATR does not clear RBF - Paula keeps reporting the same
 * character until software acknowledges, so a caller that skips this sees one
 * keystroke for ever. */
unsigned char serial_hw_rx(void);

#endif /* SERIAL_HW_H */
