/*
 * kbd.c - the keyboard, as far as the hardware goes
 *
 * The keyboard is its own little computer on the end of CIA-A's serial
 * port. It sends a byte per key transition - a 7-bit position code and an
 * up/down bit - and then waits to be told it arrived: the Amiga pulls the
 * data line low for at least 85 microseconds. No handshake and it assumes
 * the byte was lost, resends, and eventually resynchronises the hard way.
 *
 * So the handshake happens for every byte, first, whatever else is true -
 * including an event queue with no room. A keyboard must never be stalled
 * by nobody reading.
 *
 * The 85 microseconds are timed by CIA timer A, not a delay loop. A loop
 * tuned on a 7MHz 68000 is several times too short on a 68020 and hopeless
 * on a 68060; the CIA's clock is the same on all of them. It also means the
 * handler returns at once instead of spinning with everything masked.
 */
#include "kbd.h"
#include "cia.h"
#include "input.h"
#include "amiga_hw.h"

#define HANDSHAKE_USEC  100     /* 85 required */

#define CODE_RESET_WARNING  0x78
#define CODE_FIRST_PROTOCOL 0xF9

volatile unsigned long kbd_protocol_codes;
volatile unsigned long kbd_reset_warnings;

/* CIA_SERIAL: a byte has arrived. */
static void kbd_byte_isr(void *arg)
{
    unsigned char wire, code;

    (void)arg;

    /* On the wire the byte is inverted and rotated: bits 6..0 first, the
     * up/down bit last. Undo both and bit 7 is up/down, as documented. */
    wire = (unsigned char)~ciaa.sdr;
    code = (unsigned char)((wire >> 1) | (wire << 7));

    /* Start the handshake: serial port to output pulls KDAT low. Timer A
     * ends it. */
    ciaa.cra |= CIACRAF_SPMODE;
    ciaa_timer_a_oneshot(CIA_USEC(HANDSHAKE_USEC));

    if (code >= CODE_FIRST_PROTOCOL)
        kbd_protocol_codes++;
    else if ((code & 0x7F) == CODE_RESET_WARNING)
        kbd_reset_warnings++;
    else
        input_key(code);
}

/* CIA_TIMER_A: the handshake has been long enough. */
static void kbd_handshake_done_isr(void *arg)
{
    (void)arg;
    ciaa.cra &= (unsigned char)~CIACRAF_SPMODE;     /* back to input */
}

void kbd_init(void)
{
    kbd_protocol_codes = 0;
    kbd_reset_warnings = 0;

    ciaa.cra &= (unsigned char)~CIACRAF_SPMODE;
    ciaa_attach(CIA_SERIAL, kbd_byte_isr, 0);
    ciaa_attach(CIA_TIMER_A, kbd_handshake_done_isr, 0);
    ciaa_enable(CIA_SERIAL);
    ciaa_enable(CIA_TIMER_A);
}
