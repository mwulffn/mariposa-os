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

Kernel serial has two modes behind one API. Transmit first; receive has
its own section below.

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

### ser_write

`ser_write(buf, len)` is the transmit entry point; `ser_putc` is a write of
one byte. Output is split into chunks of at most 256 bytes and each chunk
goes through `write_chunk`:

1. Save SR, disable interrupts
2. While the ring has no room for the whole chunk: wait (see below)
3. Copy the chunk into the ring, advance head
4. tx_pump
5. Restore SR

**A chunk enters the ring as a unit**, inside one critical section, so two
writers cannot interleave within it. That is what keeps a `kprintf` line
whole: `kprintf` formats into a 128-byte buffer on its own stack with
interrupts on, and hands the result to `ser_write` in one piece. `\n`
expansion is kprintf's job, not the driver's.

**Step 4 is how the transmitter starts.** TBE is raised by a byte leaving the
buffer and by nothing else. Once the ISR has acknowledged the last one and
found the ring empty, no request is left to fire - so the first draft's
"enable the TBE interrupt" in ser_putc, paired with "disable it when empty"
in the ISR, sends one burst and then waits for ever. Instead INTENA's TBE bit
stays on permanently, and every write offers the UART a byte itself: on an
idle transmitter that starts it; on a busy one tx_pump does nothing and the
byte in flight raises the TBE that carries on.

**Step 2 is where the design has been wrong twice.**

The first draft said to spin until the ISR makes room, which never returns
when the caller has interrupts masked. The second version did the ISR's job
by polling from the caller - correct, and with `kprintf` holding a critical
section across the whole line, ruinous: a task printing faster than 9600
baud fills the ring at once, and from then on every line was sent by
polling at wire speed with interrupts masked. Measured under a flood of
output: a lower priority task got no CPU at all, 52 ticks arrived of 799,
and typed input was overrun in Paula's one-byte buffer.

Now it depends on who is asking:

- **A task that may sleep, sleeps**, on `tx_wait`. The TBE handler wakes all
  sleepers when the ring has drained to half - not at the first free byte,
  where a task woken per character has gained nothing over polling, and
  where one of two producers was seen to starve the other.
- **Everyone else polls**, as before, because everyone else cannot sleep: a
  handler; the kernel before `sched_start`; and any caller that arrived with
  interrupts already masked. That last one is the subtle one. It is inside a
  critical section of its own, and sleeping would switch tasks in the middle
  of it and hand its half-updated state to whoever runs next. `write_chunk`
  reads the caller's mask from the SR it saved.

Sustained output is still not free: at 9600 baud it is an interrupt per
character through the dispatcher, roughly a fifth of a 68000.

### TBE ISR (Level 1)

The assembly stub `ser_tbe_handler` raises the mask to 7, saves D0/D1/A0/A1
(vbcc's scratch registers) and calls `ser_tbe_isr`:

1. Acknowledge TBE - before looking, so a byte that frees the buffer between
   the two is a fresh request and not a lost one
2. tx_pump
3. If writers are asleep and the ring is at or below half full, wake them all

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

## Receive

The mirror image of transmit: the RBF interrupt (level 5) is the producer and
tasks are the consumers.

- **`ser_rbf_isr`** takes the byte, acknowledges RBF - reading SERDATR does
  not - puts it in a 256-byte ring and calls `wake_one`.
- **`ser_read(buf, len)`** blocks until at least one byte is available, then
  returns as many as are waiting, up to `len`. The emptiness test and the
  wait are inside one critical section, so a byte cannot arrive between them
  and leave the reader asleep on a ring that is not empty; and the test is a
  loop, because another reader may get there first. Task context only.

Paula buffers exactly one received byte, so the handler has about a
millisecond at 9600 baud. Any critical section longer than that, anywhere in
the kernel, costs input. Two counters say when and why, because "input went
missing" and "input was never sent" look the same from the far end of the
cable:

- `ser_rx_overruns` - Paula's buffer was overwritten before the handler ran
  (SERDATR OVRUN). The handler was kept waiting.
- `ser_rx_dropped` - the ring was full. Nobody was reading. The newest byte
  is the one dropped, so what was typed first survives.

The cause of overruns used to be `kprintf` itself, polling a full transmit
ring with interrupts masked; see `ser_write` above. What remains is any
caller that prints at length from inside its own critical section - the
console's `ps` is one, deliberately, to list tasks that are not changing
under it.

After a crash the ROM debugger polls the same UART with interrupts masked,
so the two never compete.

The driver is registered as `ser0`, a `chardev` (`docs/driver_design.md`),
and that is how the console task reaches it.

## Initialization

`ser_init` sets SERPER for 9600 baud, empties both rings, selects polled
mode and registers `ser0`. `irq_init` attaches `ser_tbe_isr` and
`ser_rbf_isr` and calls `ser_irq_enable` last, which enables both sources.
