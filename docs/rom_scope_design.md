# ROM Scope and Language

> **Status.** The build change and the `sprintf` conversion are done and on
> this branch. Everything under "The plan" below is a proposal, not code.

## Why

The ROM was 100% 68000 assembly: 4088 lines across ten files. That is more
fragile than it needs to be, and the fragility buys nothing measurable.

The image is **256KB and was 3.4% full** — 8848 bytes of 262144. Code size,
the usual reason to write a ROM in assembly, is not a constraint here and is
not close to becoming one. Adding a C formatter took it to 9680 bytes, 3.7%.

There is also duplication already in the tree, and more coming:

| Job | ROM | Kernel |
|-----|-----|--------|
| Serial I/O | `serial.s` | `serial.c` |
| Formatted output | `sprintf.s` | `kprintf.c` |
| Block I/O, RDB, FAT16 | `ide.s`, `partition.s`, `filesystem.s` | none yet — and the kernel cannot read a disk at all |

That last row is ~1240 lines of assembly destined to be written a second time
in C. Writing it once, in C, and linking it into both images is the whole
argument in one line.

## What the previous attempt got wrong

The `rom2c` branch is marked "failed rom conversion attempt". It compiled
`sprintf.c` to `.asm` with vbcc, **textually included** the result into
`bootstrap.s`, and commented out the generated `section CODE` directives to
force everything into one section. It also had to turn every `bsr` into `jsr`
to cope with the displacement.

That works for one function with no static data and collapses the moment
anything lands in `DATA` or `BSS`. The problem was skipping the linker, not
C. `src/kernel/` had been doing it correctly the whole time: vbcc →
`vasm -Felf` → `vlink -T kernel.ld`.

## What changed on this branch

**1. The ROM is linked.** `bootstrap.s` traded `org $FC0000` / `org $FFFFFC`
for `.text` and `.romend` sections, and `rom.ld` pins them. `bootstrap.s`
still includes the other `.s` files, so the assembly remains one object and
no cross-module `xdef`/`xref` churn was needed.

The switch was verified by producing a **byte-identical image**: same md5,
same 262144 bytes, before any C was introduced. That is the only way to know
a build change is a build change.

**2. The test harness gained a new symbol source.** `mksym.py` read the vasm
listing, which a linked build no longer has in one piece. It now also parses
a `vlink -M` map, which is strictly better: absolute addresses so nothing
needs biasing, locals included, and coverage of every object in the link.
251 symbols → 473.

This was the real risk in the whole idea. The 141 tests are the most valuable
thing in the repo and a conversion that lost them would trade one kind of
fragility for a worse one.

**3. `sprintf.s` became `sprintf.c` plus a 30-line shim.** The shim is the
part that genuinely cannot be C:

- The ~40 call sites push arguments and `bsr SerialPrintf`. Turning that
  frame into the `(format, args)` pair C takes keeps every one of them
  unchanged, and keeps this file independent of how vbcc implements varargs.
- vbcc clobbers `d0`/`d1`/`a0`/`a1` across a call; the assembly `Sprintf`
  preserved everything but its return values, and callers were written
  against that. The shim saves `d1-d7`/`a1-a6` so the contract holds.

366 lines of assembly → 175 lines of C and 30 of shim. All 56 existing format
tests passed against the C on the first run, which is what a good test suite
is for.

It also fixed a documented bug for free: `Sprintf` read its format pointer at
a fixed `60(sp)`, an offset only correct when reached by `bsr` from
`SerialPrintf`, so it could not be called directly. Both entry points now
compute their own frame. `format.sprintf_direct` pins it.

**4. `libsup.s` is shared.** vbcc emits `__divu` for the `/` and `%` in `%d`.
Rather than reimplement it, the ROM links the kernel's `libsup.s` — already
position independent, already covered by the `libsup.*` tests. This is the
duplication argument working in the intended direction.

## Space budget

Measured from `build/kick.map`, not estimated:

| | bytes |
|---|---|
| `bootstrap.o` — all ten `.s` files | 8428 |
| `sprintf.o` — C | 896 |
| `libsup.o` — shared with the kernel | 356 |
| **total code** | **9680** |
| **free** | **252,464 (96.3%)** |

The `sprintf` conversion gives a real multiplier rather than a guess: 496
bytes of assembly became 972 of C plus shim, so **~2x for hand-written
assembly to vbcc C**. At that rate a fully C ROM lands near **16KB, 6% of the
image**.

Against that, what might plausibly be added:

| | estimate |
|---|---|
| WD33C93 SCSI (A590/A2091/GVP) — selection, message and data phases | 4-8KB |
| Block-device layer, probe and boot-order table | ~1KB |
| 68000 disassembler for the debugger, table driven with mnemonics | 6-10KB |
| 8x8 font and blitter console | ~3KB |
| ROM service jump table | trivial |

SCSI *and* IDE *and* a full debugger still lands around **35-40KB, ~15%**.
Space is not the constraint and is not close to becoming one.

### What is actually scarce

1. **Low chip RAM, not ROM.** The map in `rom_design.md` reserves
   `$00000-$03FFF` and leaves **~2.75KB** free at `$3500-$3FFF`, with the
   debug bitplane already taking 10KB. Driver state and transfer buffers come
   out of that. This is the budget that will bite first.
2. **`bsr` is a 16-bit displacement, ±32KB.** Fine at 16KB. A ROM carrying
   SCSI and a disassembler would cross it and inter-module `bsr` would start
   failing - one of the things that bit `rom2c`. It is a link-time error now
   rather than silent corruption, but it is a threshold to design for.
3. **ROM shares the bus with chip RAM.** `rom_design.md` already says to
   minimise runtime ROM access. Having room is not a reason to put runtime
   services in ROM; the kernel should copy anything hot into fast RAM.

Anything in ROM also cannot be updated without rebuilding the ROM. The test
for what belongs there is narrow: **is it needed to reach the disk, or to
debug a machine whose kernel is dead?** Everything else loads from disk.

## Boot devices

Eventually IDE, SCSI, and - kept open deliberately, not planned - CompactFlash
in the A600/A1200 PCMCIA slot.

### Not via expansion ROMs

The Amiga's native answer is that the card carries its own driver, found
through autoconfig's `er_InitDiagVec` - which `autoconfig.s` already walks
past. Tempting, because it means never writing a per-controller driver.

Rejected: those ROMs are written against AmigaOS's exec and expansion ABI.
Running an A2091 or GVP boot ROM means implementing enough of AmigaOS for
third-party code to call into, which is an enormous commitment to take on for
a boot path in an OS that has its own ABI. Write our own drivers, in C,
shared with the kernel.

### Split the ATA layer from its transport

This is the decision that has to be made before a second controller exists,
and it costs almost nothing today.

`ide.s` hardcodes Gayle's mapping: base `$DA0000`, task-file registers every
four bytes from `+2`. A CompactFlash card in the PCMCIA slot is the same ATA
device speaking the same commands, but its registers are somewhere else
entirely - Gayle puts the PCMCIA I/O window at **`$A20000`**, with odd 8-bit
registers split off to **`$A30000`**, attribute memory at `$A00000`, card
control at `$DA8000` and card reset at `$A40000`.

So the conversion of `ide.s` should produce two pieces:

- **ATA protocol** - LBA28 PIO read, status and DRQ polling, command issue.
  Identical across Gayle IDE, PCMCIA ATA and a CF adapter on the IDE port.
- **Transport** - how a task-file register is reached and how a data word is
  read. Gayle IDE and PCMCIA differ here and nowhere else.

A base-and-stride pair, or a small accessor struct, is enough. Retrofitting
this after `partition.s` and `filesystem.s` have been written against a
Gayle-shaped API is the expensive version.

PCMCIA would additionally need card-present detect, a reset, a CIS tuple walk
in attribute memory (8-bit on a 16-bit bus, so every other byte), and writing
the card configuration index - perhaps 1-2KB, and purely additive once the
transport split exists. FS-UAE can emulate it (`pcmciaide`), so it is
testable if it ever gets built; the headless harness would need a model of
the `$A20000` window.

## The plan

**Stays assembly, permanently** (~350 lines). Not because assembly is nicer,
but because C cannot express it:

- Reset entry before RAM is usable: initial SSP, ROM overlay clear.
- A `romcrt0.s` once any C needs writable statics — `.data` copy out of ROM,
  `.bss` clear. Nothing needs this yet; `sprintf.c` has no mutable statics.
- `panic.s`'s exception entry stubs. A register dump has to capture registers
  before anything else touches them.
- `sprintf_glue.s` and anything else crossing a hand-rolled ABI.

**Moves to C, ROM-only:** `autoconfig.s` (256), `memory.s` (351), and the
boot orchestration in `bootstrap.s`.

**Moves to C, shared source with the kernel:** `serial.s` (220), `ide.s`
(260), `partition.s` (362), `filesystem.s` (622). One source, two linker
scripts. This is where the duplication in the table above dies.

**Probably leaves the ROM entirely:** `debugger.s` (637). A minimal serial
debugger belongs in ROM as the last resort for when the kernel is dead; the
command set, history and eventual disassembler do not.

### Order

Convert in dependency order, cheapest first, and keep the suite green at
every step. `sprintf` was chosen to go first precisely because it was
self-contained, heavily tested, and the thing `rom2c` had already failed at.

1. ~~`sprintf.s`~~ — done.
2. `serial.s`, shared with the kernel's `serial.c`. The `format.put_*` tests
   already cover it, and it establishes the `volatile` register-access
   pattern the drivers need.
3. `ide.s`, shared — **before** the layers above it, so the ATA/transport
   split lands while there is only one implementation to shape it around.
   The `disk.*` tests cover it against a generated image.
4. `partition.s` and `filesystem.s`, shared, written against the block-device
   interface rather than against Gayle.
5. `autoconfig.s` and `memory.s`.
6. Split the debugger.

### Rules

- **Never convert a routine the tests do not already cover.** Write the test
  against the assembly first, watch it pass, then swap the implementation.
  That is what made the `sprintf` conversion a non-event.
- **No writable statics in ROM C** until `romcrt0.s` exists. Scratch goes in
  chip RAM at a fixed address, as `SPRINTF_BUFFER` does.
- **Shared code lives in one file** compiled against two linker scripts, not
  copied. It goes in `src/shared/`.
- **Share mechanism, never policy.** The ROM polls and must keep working when
  the kernel is dead; the kernel wants interrupts and buffering. Serial is the
  first case and the template: `serial_hw.c` knows how to hand the UART a
  byte, and nothing about how to wait. The same split is coming for block
  I/O - ATA protocol is shared, transport and blocking are not.
- **Keep `bsr` reachable.** It is a 16-bit displacement, ±32KB. At 9680 bytes
  there is enormous headroom, but the ROM growing past 32KB of code would
  start breaking `bsr` to distant symbols, which is one of the things that
  bit `rom2c`.
