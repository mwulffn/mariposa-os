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
   already cover it.
3. `partition.s` and `filesystem.s`, shared. The `disk.*` tests cover them
   against a generated RDB + FAT16 image, so this is the same bet as sprintf.
4. `ide.s`, shared. Needs `volatile` register access; the kernel's serial
   code already establishes the pattern.
5. `autoconfig.s` and `memory.s`.
6. Split the debugger.

### Rules

- **Never convert a routine the tests do not already cover.** Write the test
  against the assembly first, watch it pass, then swap the implementation.
  That is what made the `sprintf` conversion a non-event.
- **No writable statics in ROM C** until `romcrt0.s` exists. Scratch goes in
  chip RAM at a fixed address, as `SPRINTF_BUFFER` does.
- **Shared code lives in one file** compiled against two linker scripts, not
  copied.
- **Keep `bsr` reachable.** It is a 16-bit displacement, ±32KB. At 9680 bytes
  there is enormous headroom, but the ROM growing past 32KB of code would
  start breaking `bsr` to distant symbols, which is one of the things that
  bit `rom2c`.
