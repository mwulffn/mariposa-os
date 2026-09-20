# Serial Subsystem Design

> **Status.** Implemented in `src/kernel/serial.c` and
> `src/kernel/vectors.s`, pinned by the `kser.*` tests against the real
> kernel image, and verified under FS-UAE. The ROM side still polls, by
> design. Three things below differ from the first draft of this document,
> each because the draft would not have worked: how the transmitter is
> restarted, what happens when the ring is full, and what a crash does with
> bytes still queued.

## What ROM and kernel share, and what they must not

`src/shared/serial_hw.c` holds the register primitives: set the divisor, ask
whether TBE or TSRE or RBF is set, hand over a byte, take a byte. Nothing in
it blocks, allocates or keeps state.

**The waiting strategy is deliberately not shared.** The two sides need
opposite things:

| | ROM | Kernel |
|---|---|---|
| Strategy | poll | ring buffer + level 1 TBE interrupt |
| Why | the debugger must work on a machine whose kernel has died, with interrupts in an unknown state | kprintf must not block callers at 9600 baud |
| Depends on interrupts | never | yes |

Sharing the strategy would give the kernel an inherited spin loop, or give
the ROM a dependency on interrupts working at the exact moment they are least
likely to. So `src/rom/serial.c` polls, `src/kernel/serial.c` buffers,
and both sit on the same primitives underneath.

The kernel still needs a polled path for panic, before interrupts are up and
after they have stopped being trustworthy. That is the same primitives called
directly, not a second driver.

## Acknowledging received bytes

`serial_hw_rx()` writes `INTREQ` to clear RBF as part of taking the byte.
This is not optional: RBF mirrors `INTREQ` bit 11, reading `SERDATR` has no
side effect, and Paula keeps reporting the same character until software
acknowledges. A receive path that skips it reads one keystroke for ever.

## Overview

Kernel serial is transmit-only and has two modes behind one API.

- **Polled**, from `ser_init()` until `ser_irq_enable()`. Early boot runs with
  the CPU masked; output is on the wire when `ser_putc` returns, so an early
  crash loses nothing.
- **Interrupt-driven** afterwards. A 1024-byte ring buffer decouples kprintf
  callers from the 9600 baud wire speed, drained by the level 1 TBE
  interrupt.

Every function is safe in any context: interrupts on or masked, task or ISR.

## Hardware

- Paula UART at $DFF000
- 9600 baud, 8N1
- TBE (Transmit Buffer Empty) interrupt at level 1, vector $64

## Interrupt-Driven Transmit

### Data Structures

- 1024-byte ring buffer in the kernel's .bss (fast RAM)
- Head index: advanced by ser_putc (producer)
- Tail index: advanced by whoever hands a byte to the UART (consumer)
- One slot is kept empty, so head == tail means empty and never full

### tx_pump

The one place a byte moves from ring to UART: if the ring is non-empty **and
SERDATR says TBE**, write the tail byte to SERDAT. Otherwise do nothing.

It asks the UART every time and never trusts the interrupt to mean "the
buffer is free". The ROM polls the same UART, INTREQ bits can be set by
software, and a request can be left over from either; writing SERDAT with a
byte still in the buffer destroys that byte silently.

### ser_putc

1. Save SR, disable interrupts
2. While the ring is full: poll until TBE, then tx_pump
3. Write byte to the ring, advance head
4. tx_pump
5. Restore SR

`\n` expansion is kprintf's job, not the driver's.

**Step 4 is how the transmitter starts.** TBE is raised by a byte leaving the
buffer and by nothing else. Once the ISR has acknowledged the last one and
found the ring empty, no request is left to fire - so the first draft's
"enable the TBE interrupt" in ser_putc, paired with "disable it when empty"
in the ISR, sends one burst and then waits for ever. Instead INTENA's TBE bit
stays on permanently, and every ser_putc offers the UART a byte itself: on an
idle transmitter that starts it; on a busy one tx_pump does nothing and the
byte in flight raises the TBE that carries on.

**Step 2 is why a full ring cannot deadlock.** The first draft said to
spin-wait for the ISR to make room, which never returns when the caller has
interrupts masked - kprintf inside a critical section or an ISR. Doing the
ISR's job by polling keeps order, because it is the same ring and the same
end of it. Step 4 also keeps output moving while the CPU is masked.

### TBE ISR (Level 1)

The assembly stub `ser_tbe_handler` raises the mask to 7, saves D0/D1/A0/A1
(vbcc's scratch registers) and calls `ser_tbe_isr`:

1. Acknowledge TBE - before looking, so a byte that frees the buffer between
   the two is a fresh request and not a lost one
2. tx_pump

Ring empty: nothing happens and the transmitter goes quiet. UART not ready: a
stale request; the real one is still coming. Both are tx_pump doing nothing.

The mask goes to 7 because the 68000 only raised it to 1. A vertical blank
handler that calls kprintf would otherwise enter ser_putc with the ISR
halfway through moving the tail. This does not contradict rule 3 of
`interrupt_control_design.md` so much as sharpen it: an ISR is only protected
from its own level and below.

### ser_irq_enable

Sets INTENA TBE, and if bytes are already queued, **sets INTREQ TBE by hand**.
`irq_init` clears INTREQ wholesale before enabling anything, which can throw
away exactly the request a queued byte was waiting on. Raising it in software
runs the ISR, which looks at the UART and carries on from wherever it really
is. Safe to call again at any time.

### ser_flush

Masks interrupts, pumps the ring dry by polling, then waits for TSRE - the
shift register, not just the buffer, so the last character is really on the
wire. Works when interrupts do not. Call it before handing the UART to
anyone else.

### Buffer Sizing

At 9600 baud, one byte takes ~1.04ms to transmit, so the ring holds about a
second of output. A burst longer than that slows the producer to wire speed
for the excess - it is never dropped.

## Crash Mode: ROM Debugger

The ROM debugger polls the UART directly:

- Waits for TSRE in SERDATR
- Writes directly to SERDAT
- No ring buffer, no interrupts, no dependencies on kernel state

It knows nothing about the kernel's ring. Left at that, a crash would print
the register dump and drop up to a second of the output that led to it - the
lines that matter most. So `trap_init()` (`vectors.s`, called from
`irq_init`) wraps vectors 2-11 with stubs that call `ser_flush` and then
continue into the ROM handler they replaced, **with the exception frame
untouched**: each stub pushes the old handler's address and the common tail
`rts`es into it, so the ROM decodes the six- or fourteen-byte frame as if
nothing had intervened. A guard flag skips the flush if the fault came from
inside the flush.

`crt0.s` flushes the same way before calling `rom_panic` if `kernel_main`
returns.

What is still lost: a crash that arrives by some other road - the ROM's
autovector panic for an interrupt nobody installed, or a wild jump straight
into the debugger.

## No Receive

The kernel does not receive serial data in normal operation. Serial receive
is handled exclusively by the ROM debugger in crash mode. `ser_getc` exists
and polls. An RBF-driven receive ring (level 5) is the obvious next step once
something in the kernel wants input.

## Initialization

`ser_init` sets SERPER for 9600 baud, empties the ring and selects polled
mode. `irq_init` installs the level 1 vector and calls `ser_irq_enable` last.
