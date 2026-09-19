/*
 * serial.c - kernel serial output
 *
 * Polled today. This is the half of serial that is NOT shared with the ROM,
 * and deliberately so: the target here is a ring buffer drained by the level
 * 1 TBE interrupt, so kprintf is not held hostage to 9600 baud, while the
 * ROM must keep polling because its debugger has to work on a machine whose
 * kernel has already died. See docs/serial_design.md.
 *
 * Everything below the waiting strategy lives in src/shared/serial_hw.c, so
 * when the interrupt path lands only this file changes.
 */

#include "serial.h"
#include "serial_hw.h"

void ser_init(void)
{
    /* The ROM has already done this; make the state explicit anyway. */
    serial_hw_init(SERIAL_BAUD_9600);
}

void ser_putc(char c)
{
    /* Spin until the UART will take it. Replaced by an enqueue onto the ring
     * buffer once interrupts exist - see docs/serial_design.md. */
    while (!serial_hw_tx_ready())
        ;
    serial_hw_tx((unsigned char)c);
}

void ser_puts(const char *s)
{
    while (*s)
        ser_putc(*s++);
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
