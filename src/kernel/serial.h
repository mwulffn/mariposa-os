/*
 * serial.h - Serial port output
 */

#ifndef SERIAL_H
#define SERIAL_H

/*
 * Initialize serial port.
 * Call this even though ROM sets it up - ensures known state.
 */
void ser_init(void);

/*
 * Output single character. Blocks until transmit buffer ready.
 */
void ser_putc(char c);

/*
 * Output null-terminated string.
 */
void ser_puts(const char *s);

/*
 * Check if receive buffer has data.
 */
int ser_can_read(void);

/*
 * Read character. Blocks until data available.
 */
char ser_getc(void);

#endif /* SERIAL_H */
