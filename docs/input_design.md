# Input Design

> **Status.** Keyboard implemented: `cia.c`, `kbd.c`, `input.c`, `keymap.c`,
> pinned by `kbd.*`. US and Danish layouts. Verified by hand under FS-UAE
> with the console's `keys` command: keys, qualifiers and repeat arrive as
> they should. **Dead keys have only ever run in the harness** - no dead key
> could be found through the emulator's host key mapping - so they, and the
> Danish table as a whole, are still to be confirmed on real hardware. The
> boot console's `con0` device (`docs/display_design.md`) is the first real
> consumer of key events. Mouse and joystick are not
> started. Nothing consumes events yet except that command - there is no
> screen console to type into.

## Three layers

```
keymap.c   data: what is printed on the keys
input.c    qualifiers, key repeat, translation -> events, a queue, blocking read
kbd.c      the hardware protocol -> raw key codes
cia.c      CIA-A's interrupt register, which the keyboard shares
```

### The driver deals in raw key codes and nothing else

The Amiga keyboard sends a 7-bit **position** code and an up/down bit. `$10`
is "the key where Q is on a US board", whatever is printed on it - on a
French keyboard that keycap says A. The hardware is layout independent
already; a driver that produced characters would be throwing that away, and
every new layout would mean touching driver code.

`kbd.c` therefore knows the wire format (the byte arrives inverted and
rotated), the handshake, and the bytes that are the keyboard talking about
itself (`$F9` resend, `$FA` buffer overflow, `$FC` self test failed,
`$FD`/`$FE` power-up stream, `$78` reset warning). Those are counted, never
delivered as keys.

### Events carry both the code and the character

```
struct input_event { type; code; value; qual; ch; }
```

`code` is the raw position, `value` is up / down / repeat, `qual` is the
qualifiers held, `ch` is a Unicode code point or 0. Both, because both have
users: a window manager or a game binds by position - WASD has to stay
WASD-shaped on AZERTY - and a text field wants the letter. Translating once,
centrally, means neither consumer reimplements the other half, and a key
with no character (F1, cursor keys) is still an event.

### Characters are Unicode code points

16 bits in the tables and the event. Latin-1 is the first 256 code points, so
an 8-bit Amiga-style consumer truncates and a UTF-8 consumer (Wasm
applications, most likely) encodes from the same value. The text encoding
question stays out of the keyboard.

### Keymaps are data

A `struct keymap` is a name and a table indexed by raw code: plain, shift,
alt, and flags. `KF_CAPS` marks letters - caps lock is shift for those and
nothing at all for the rest, which is what makes caps-lock-Æ work and
caps-lock-1 stay 1. `KF_DEAD_*` marks a level as a dead key.

**Dead keys.** A dead key produces an event with `ch` 0 and waits. The next
key resolves it: accent + letter gives the combined letter if Latin-1 has
one; accent + space gives the accent itself; accent + anything else gives
the anything else, accent dropped. A dead key does not repeat.

Two layouts ship, US and Danish, and the second is the point: one layout
proves nothing about the abstraction. The Danish table follows the standard
Danish layout and has **not** been checked key by key against a Danish Amiga
keyboard; `$00`, `$0D`, `$2B` and `$30` are the corners to look at. Adding a
layout is a table in `keymap.c` and a line in `input.c`'s list.

## The handshake is timed by the CIA, not by a loop

After each byte the Amiga must pull the keyboard's data line low for at
least 85 microseconds. The usual implementation is a delay loop. A loop tuned
on a 7MHz 68000 is several times too short on a 68020 and hopeless on a
68060 - `kbd.handshake.68020` and `.68040` fail against exactly that
mutation, while `.68000` passes. CIA-A timer A runs as a one-shot for 100
microseconds and its interrupt ends the pulse: the same duration on every
CPU, and the handler returns at once instead of spinning with everything
masked.

The handshake happens for every byte, first, whatever else is true -
including an event queue with no room. A keyboard must never be stalled by
nobody reading; events are dropped and counted (`input_dropped`) instead.

## CIA-A's interrupt register has one owner

CIA-A puts timer A, timer B, the TOD alarm, the serial port and the FLAG pin
behind one register, and **reading it clears all five**. A keyboard driver
that read ICR would silently acknowledge the timers; a timer driver would
swallow keystrokes. `cia.c` reads it once per interrupt and hands each flag
to whoever attached (`ciaa_attach`), which is the driver doc's rule for a
register shared between devices.

Order matters: read ICR, *then* clear PORTS in Paula. The CIA's interrupt
line is a level, and Paula sets PORTS straight back while it is asserted.

## Qualifiers and repeat

`$60`-`$67` are the qualifier keys in `QUAL_` bit order. Caps lock needs no
toggling: the keyboard keeps that state itself and sends "down" when the
light goes on and "up" when it goes off.

The Amiga keyboard does not repeat; `input_tick` does, off the vertical
blank: 600ms, then about 17 a second. The last key down is the one that
repeats, qualifiers never do, and the character is looked up afresh each
time, so pressing shift mid-repeat changes what repeats.

Ctrl with a character from `@` to DEL gives the control code: ctrl-C is 3.

## Open

- There is no way to block on two sources at once, so the console's `keys`
  command polls at tick rate. A wait-on-several primitive arrives with the
  first thing that needs it properly.
- Mouse and joystick, as `inputdev` events in the same queue.
- A screen console, which is what makes the keyboard useful.
