/*
 * serial.c - ROM serial output
 *
 * Polled, and staying that way. The ROM's serial has to work when the kernel
 * has died and interrupts are in an unknown state - the debugger reaches the
 * UART by banging registers, which is the one path that must never depend on
 * anything else working. The kernel's copy goes the other way, to a ring
 * buffer drained by the TBE interrupt. See docs/serial_design.md.
 *
 * What both sides do share is src/shared/serial_hw.c, which knows how to
 * hand the UART a byte and nothing about how to wait.
 *
 * serial_glue.s exports the register-convention entry points that panic.s
 * and debugger.s call; this file is plain C.
 */

#include "serial_hw.h"

typedef unsigned long u32;

void rom_serial_init(void)
{
    serial_hw_init(SERIAL_BAUD_9600);
}

/*
 * Waits on TSRE, the transmit *shift* register, not TBE. Stricter and much
 * slower than it needs to be for throughput - but this is the path a panic
 * uses, and a machine that stops immediately after the last character should
 * still have put that character on the wire. The kernel, which cares about
 * throughput and is not usually about to die, waits on TBE instead.
 */
void rom_serial_put_char(u32 c)
{
    while (!serial_hw_tx_drained())
        ;
    serial_hw_tx((unsigned char)c);
}

void rom_serial_put_string(const char *s)
{
    while (*s != '\0')
        rom_serial_put_char((unsigned char)*s++);
}

/* Non-blocking. Returns 0 when nothing is waiting, as the assembly did -
 * which means a received NUL is indistinguishable from no data. Kept because
 * changing it would be a silent change to a documented interface. */
u32 rom_serial_get_char(void)
{
    if (!serial_hw_rx_ready())
        return 0;
    return serial_hw_rx();
}

u32 rom_serial_wait_char(void)
{
    while (!serial_hw_rx_ready())
        ;
    return serial_hw_rx();
}

void rom_serial_put_hex(u32 value, u32 digits)
{
    int shift = (int)(digits - 1) * 4;

    for (; digits > 0; digits--, shift -= 4) {
        unsigned nibble = (unsigned)((value >> shift) & 0xF);
        rom_serial_put_char(nibble < 10 ? '0' + nibble : 'A' - 10 + nibble);
    }
}

/* Unsigned, full 32-bit range. Ten digits covers 4294967295. */
void rom_serial_put_decimal(u32 value)
{
    char digits[10];
    int n = 0;

    if (value == 0) {
        rom_serial_put_char('0');
        return;
    }

    while (value != 0) {
        digits[n++] = (char)('0' + (unsigned)(value % 10));
        value /= 10;
    }

    while (n > 0)
        rom_serial_put_char((unsigned char)digits[--n]);
}
