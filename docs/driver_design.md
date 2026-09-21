# Driver Architecture

> **Status.** Implemented so far: the interrupt layer (`irq.c`, pinned by
> `kirq.*`), the device registry (`dev.c`), the `chardev` class, and serial
> as its first instance, `ser0` - transmit and receive, the first driver
> written to the pattern below - with a console task on top
> (`console.c`). The keyboard is the second driver and the first on a shared
> CIA - see `docs/input_design.md`; its events go through `input.c` and not
> yet through a registered `inputdev`. `blkdev` exists only in the ROM's boot
> path. The GUI / blitter layer is deliberately open.

## The budget

AmigaOS is thin because it had to fit 512KB and a ROM. This system targets a
machine with fast RAM, so it has room AmigaOS did not - but only of one kind.
The CPU is still a 7MHz 68000, chip RAM is still about a megabyte, and the
blitter and every other DMA device can reach only chip RAM.

**Spend fast RAM to save cycles and chip RAM.** Keep state rather than
recompute it; keep names, logs and checks that a 512KB system could not
afford. Do not spend cycles to save memory, and do not put anything in chip
RAM that the custom chips do not need to see.

## Every driver is two halves and a queue

`src/kernel/serial.c` is the template.

- **Bottom half** - the interrupt handler. Acknowledges the hardware, moves
  data between the device and a ring buffer or the current request, starts
  the next queued hardware job if there is one, and calls `wake_one` /
  `wake_all`. It never blocks, never allocates, never calls another driver.
- **Top half** - runs in the calling task. Validates, queues work, sleeps on
  the driver's wait queue until the bottom half has made progress, copies
  out.
- **A polled path** that works with interrupts dead, for panic and early
  boot. Serial has one; the block driver's is the `ata.c` it shares with the
  ROM.

## What "async" means here

The devices that benefit from asynchronous operation are the ones with DMA,
which work on their own and interrupt when finished:

| Device | Shape |
|---|---|
| Blitter | a queue of blits; the BLIT interrupt starts the next one straight from the handler, so the blitter never idles waiting for a task to be scheduled |
| Audio | a chain of buffers; Paula interrupts when it has latched one, leaving a full buffer's time to supply the next |
| Floppy | a track read by disk DMA, plus CIA-timed head steps; tens to hundreds of milliseconds - a task sleeping on completion |

What does not: Gayle IDE is PIO with no DMA, so only the wait for the drive
can be asynchronous and a blocking task gets all the benefit there is.
Serial, keyboard and mouse are push streams, not request and response.

So there is **no generic IO-request layer**, exec.library style. The devices
that want asynchrony want a class-specific work queue drained by their own
handler, not a message and a reply port per operation. What callers share is
one small primitive - a completion: done flag, status, wait queue. Submit and
carry on, or submit and wait; "synchronous" is the second. It arrives with
its first user, which will be the blitter.

## Interrupts are shared, so they are dispatched

Paula folds fourteen sources onto six CPU levels: level 1 carries serial
transmit, disk block and the software interrupt; level 3 carries vertical
blank, copper and blitter. One handler per vector cannot work.

```
void irq_attach(unsigned int source, void (*handler)(void *), void *arg);
void irq_enable(unsigned int source);
void irq_disable(unsigned int source);
```

`source` is an INTENA bit number. One assembly entry per level masks
interrupts, calls `irq_dispatch(level)`, and leaves through `isr_exit`, so
every handler gets the scheduler's switch-on-exit for free. The dispatcher
calls the handler of each source on that level that is both enabled and
pending.

- **The handler acknowledges its own source.** How and when is device
  knowledge: serial must acknowledge before it looks at the UART; a CIA is
  acknowledged by reading its ICR.
- **Nobody writes INTENA but `irq.c`.** A driver owns its device's registers
  exclusively; a register shared between devices has one owner.
- **The cost is measured, not assumed.** Going through C costs a vertical
  blank about 2,150 cycles on a 68000, entry to exit - 1.5% of the machine
  at 50Hz. `task.tick_cost_bounded` fails if it passes 4,000.
- **An enabled source with no handler is switched off and counted**
  (`irq_spurious`). Unacknowledged, it would re-enter for ever.

## Typed device classes, not one file interface

```
chardev    read, write, ready          serial, then a console
blkdev     read/write blocks, geometry Gayle IDE   (src/shared/blkdev.h)
inputdev   an event stream             keyboard, mouse
```

Each class is a struct of function pointers and an opaque hardware pointer,
as `blkdev.h` already is, plus a small registry - `dev_register(class, name,
ops)`, `dev_find(name)` - so that "a console on `ser0`" and "mount `ide0p1`"
can be said without naming a driver.

Not everything-is-a-file: on this hardware it degenerates into `ioctl` for
everything that matters, and its payoff needs a VFS nothing here has. A file
layer can be put over `chardev` later. Not driver-as-task: two context
switches per operation at 7MHz, and without an MMU it isolates nothing.

**Applications never see any of this.** Wasm applications get host calls
into subsystems - graphics, input, filesystem. The driver interface is
internal and free to change; the host-call ABI is the one that must not.

## Open: the GUI layer

Not settled, and it shapes the blitter driver, so that is not designed yet
either. What the budget rule already suggests:

- **Retained, not repaint-on-demand.** AmigaOS made applications redraw
  because it could not afford to remember window contents. Here the scene -
  a text cell grid, a display list, a glyph cache - can live in fast RAM and
  the system repaints from it.
- **Tiling avoids the expensive part.** No overlap means no obscured
  regions, no damage repair and no backing store. A backing store in fast
  RAM would be the wrong trade regardless: the blitter cannot reach it, so
  restoring from it is a CPU copy.
