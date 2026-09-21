/*
 * kbd.h - the keyboard, as far as the hardware goes
 *
 * Raw key codes and nothing else: docs/input_design.md. Characters,
 * qualifiers, repeat and layouts are input.c's.
 */
#ifndef KBD_H
#define KBD_H

/* After cia_init() and input_init(). */
void kbd_init(void);

/* Bytes that were the keyboard talking about itself, not keys: $F9 resend,
 * $FA buffer overflow, $FC self test failed, $FD/$FE power-up key stream.
 * A count that moves in normal use means codes are being lost. */
extern volatile unsigned long kbd_protocol_codes;

/* $78: ctrl-amiga-amiga. The keyboard is about to reset the machine. */
extern volatile unsigned long kbd_reset_warnings;

#endif /* KBD_H */
