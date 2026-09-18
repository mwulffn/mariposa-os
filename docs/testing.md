# Testing

Two tiers. The fast one needs no Amiga at all.

| Tier | What it covers | Cost | Command |
|------|----------------|------|---------|
| Headless CPU | ROM routines and the kernel's libsup.s, as real 68000 code, incl. the whole IDE/RDB/FAT16 path | ~0.3s for the whole suite | `make test` |
| FS-UAE | Boot path, real hardware behaviour | seconds, needs a display | `./debug.py`, `test_*.py` |

The FS-UAE tier is three scripts:

| Script | Checks |
|--------|--------|
| `test_memory_config.py` | The memory map table, entry by entry, against the sizes in whichever config `make run` launches |
| `test_comprehensive.py` | The debugger's command set over serial |
| `test_serial.sh` | That serial output arrives at all |

`test_memory_config.py` derives what it expects from the config file rather
than hardcoding it. The previous version hardcoded four addresses the ROM had
stopped using, and had been silently wrong for a long time - which is the
failure mode to design against in this tier, since nothing runs it
automatically.

Everything that is pure logic belongs in the first tier. Reserve the emulator
for things only real hardware can answer.

## Headless tests

```bash
make test                     # build the ROM, run everything
make test FILTER=rom.panic    # one group, while iterating
```

The suite runs the actual `kick.rom` image under the
[Musashi](https://github.com/kstenerud/Musashi) 68000 core. There is no
emulator, no display and no serial port: a routine is called directly, with
arguments placed on a real guest stack, and the result read back out of guest
memory or captured serial output.

Musashi is MIT licensed and is fetched on demand into `tools/musashi/`
(gitignored, not vendored). The first `make test` needs network for that one
clone; after that it is offline. The container image pre-fetches it, so
`make docker-make DOCKER_TARGET=test` works offline from the start.

### The ### protocol

Every line on stdout beginning with `###` is machine readable and exactly one
line long, so a harness or an agent can parse results without guessing at a
terminal stream:

```
###BOOT ok
###TEST format.decimal_small PASS
###TEST rom.panic_group0_frame FAIL test_vectors.c:141 ... expected $0012345A got $00BADBAD
###TEST format.decimal_large XFAIL FormatDecToBuffer uses divu.w: ...
###PANIC ADDRESS ERROR (vector 3)
###DONE pass=34 fail=0 xfail=11 xpass=0
```

Exit codes:

| Code | Meaning |
|------|---------|
| 0 | everything passed |
| 1 | at least one test failed |
| 2 | harness error — no ROM, no symbols, unknown symbol name |

The split between 1 and 2 is deliberate: "my code is wrong" and "the harness
is broken" should never look alike.

### XFAIL

A test marked `xfail` carries a one-line description of the known bug that
makes it fail. It is reported but does not fail the run, so the suite works as
a regression gate while the bug stays visible and documented.

If an xfail test starts passing it is reported as `XPASS`, with a `###NOTE`
telling you to drop the annotation. XPASS does **not** fail the run — fixing a
bug should never look like breaking something.

### Writing a test

Add a function and a row in the table at the bottom of the file:

```c
static void t_hex_default_is_long(void)
{
    CHECK_STR("DEADBEEF", rom_printf1("%x", 0xDEADBEEFu));
}

static const test_case tests[] = {
    { "hex_default_is_long", t_hex_default_is_long, NULL },  /* NULL = must pass */
    { "decimal_large",       t_decimal_large,       "divu.w overflows at 655360" },
};
```

Routines are reached by name through `h_sym()`, resolved from a symbol table
the ROM build emits (`src/rom/build/kick.sym`, produced by `tests/mksym.py`
from vasm's listing). An unknown name is a harness error with a clear message,
not a silent call to address zero. Only global labels and equates are
exported — vasm does not emit local labels, so `.foo` is unreachable.

`h_call()` pushes a return sentinel and runs until the routine returns;
`h_run()` is for code that never returns, like `panic`.

### What the machine model covers

Deliberately small. Anything a routine touches that is not modelled is
recorded as a fault and fails the test, rather than silently reading zero.

- **RAM** at `$000000`–`$1FFFFF` and `$200000`–`$2FFFFF`, **ROM** at `$FC0000`
  (writes to it are a fault).
- **Paula's UART**: `SERDAT` writes are captured as the test's output, and
  `SERDATR` reports the transmitter permanently ready. Note the byte-wide
  `btst` polling in `serial_put_char` reads the high half, so the model
  handles byte and word access separately.
- **Exception vectors** point at per-vector sentinels, so an exception is
  identified by where PC lands — no frame decoding, and the vector number is
  reported by name.
- **Misaligned access** is caught in the memory callbacks: a word or long
  access to an odd address is an address error on a 68000 and fails the test.
- **A Gayle-mapped ATA disk** over a raw image file: the LBA28 PIO read path
  `ide.s` implements, with BSY never asserted and no interrupts or DMA. With
  no image attached the status register reads `$7F`, which `ide.s` treats as
  "no drive", so tests that do not care about disks are unaffected and none
  of them hang.
- **Stubs** that exist only to stop routines hanging: CIA-A/B and Zorro
  autoconfig space (reports no card).

One deliberate deviation from hardware: the model clears RBF when the guest
reads `SERDATR` as a word, whereas Paula needs an `INTREQ` write to ack.
Without it `serial_get_char`, which never acks, would spin forever. A test of
the ack path itself has to check `INTREQ` directly.

### Testing code outside the ROM

`libsup.s` lives in `src/kernel/`, not the ROM, and holds the 32-bit divide
and modulo helpers vbcc emits calls to. It is position independent - only
PC-relative branches, no data references - so the tests assemble it
standalone to origin zero, load it at `$100000` with `h_load_module()`, and
merge its symbols with a matching bias via `h_add_symbols()`. No C cross
compiler needed, so these run even where vbcc is not installed.

The same mechanism will take `SYSTEM.BIN` once there is a reason to call into
a built kernel.

### The disk tier

`tests/mkdisk.py` builds the images: a raw IDE disk carrying an Amiga Rigid
Disk Block, one partition, and a FAT16 filesystem with `SYSTEM.BIN` on it —
exactly the shape `partition.s` and `filesystem.s` expect. No `boot.hdf`, no
mtools, no emulator. It also emits `disk_layout.h`, so the tests assert
against the values actually written rather than a second, drifting copy.

Two properties of the generated image do real work:

- **`SYSTEM.BIN`'s cluster chain is scattered, not contiguous** (2, 9, 3, 15,
  4, …), so a broken `fat16_get_next_cluster` cannot pass by reading straight
  through.
- **Its contents are position dependent**, so clusters loaded out of order or
  skipped are detectable byte for byte.

The root directory also carries a volume label, a deleted entry and a second
file ahead of `SYSTEM.BIN`, so the directory scan has something to skip. That
second file's FAT entry sits in a different FAT sector from the chain's,
which is the only thing that makes the single-sector FAT cache in
`fat16_get_next_cluster` reload.

There is no mtools here to cross-check the image against, so `mkdisk.py`
self-checks: it re-reads what it wrote through the BPB rather than through
its own constants, walks the FAT chain, and reassembles the file. The ROM's
parser agreeing with an independently written reader is the evidence that
the layout is right.

**The byte order detail that makes it work.** A word read of the data port
returns the two bytes high byte first, so a `move.w IDE_DATA,(a0)+` leaves
memory holding the sector byte for byte. That is what lets the big-endian
RDB compare (`move.l` against `'RDSK'`) and the little-endian FAT parsing
(assembled byte by byte) both work off the same buffer. Get it backwards and
one of the two breaks — which is exactly the bug class this tier exists to
catch.

## What the suite found

Everything it turned up has been fixed, and the tests that found each bug now
guard the fix. The suite is 127 tests, no xfails.

The storage path was in good shape from the start: `ide.s`, `find_rdb`, the
FAT16 boot-sector parse, the directory scan, the chain walk and
`load_system_bin` all passed against a real generated image first try,
including loading a ten-cluster scattered file to `$200000` in the right
order. The formatting code was where the bugs lived.

**Two 16-bit overflows**, both of instructions that fail quietly:

- `load_partition` computed `LowCyl * Heads * Sectors` with chained `mulu.w`,
  which is 16x16, so the intermediate truncated. At 20000 cylinders and 4
  heads, 80000 became 14464 — any partition more than roughly 2GB into a disk
  read from the wrong place.
- `FormatDecToBuffer` and `serial_put_decimal` used `divu.w #10`, which is
  32/16 → 16 and overflows once the quotient passes 65535, i.e. from 655360
  up. On overflow the 68000 leaves the destination untouched, so the digits
  were garbage rather than obviously wrong.

Both now go through `src/rom/math.s` (`mul32x16`, `divu32_10`), swept against
host arithmetic over a few thousand values.

**A format syntax that did not match its own documentation.** `sprintf.s`
accepted the size modifier only *before* the specifier (`%.lx`), while almost
every caller wrote it after (`%x.l`, `%x.b`). Those printed a full longword
followed by the modifier as literal text. The parser now accepts both
spellings — but only after `%x` and `%b`, where a size means something, so
`"%s.bin"` and `"%d.log"` keep their dots instead of losing two characters.

**Two narrower bugs in the same area.** `%.bx` and `%.wx` read a *word* from
a longword stack slot and got the high half; every path now reads a long and
narrows. And the width parser consumed a single digit, so `%08x` read `0` as
the width and `8` as the specifier and dropped the whole thing; it now parses
multiple digits, clamped to 8.

**`libsup.s` came out clean.** Twenty tests over the four helpers - both
paths of each (the `divu.w` fast path and the shift-subtract fallback), the
sign combinations for the signed versions, divide by zero, and 6000 swept
random cases against host arithmetic. No failures.

That was not the expected result. The shift-subtract loop keeps its remainder
in a register the same width as the divisor, which looks like it should lose
the top bit whenever the divisor exceeds 2^31. It does not: after k dividend
bits have been consumed the partial remainder is below 2^k, so before the
final shift it is always under 2^31 and `2r + b` always fits. The cases that
would have exposed it if the reasoning were wrong - `0xFFFFFFFF / 0x80000001`
and friends - are in the suite explicitly.

One gap rather than a bug: libsup exports only `__divu`, `__divs`, `__modu`
and `__mods`. There is no 32x32 multiply helper, and the 68000 has no
`mulu.l`. Nothing in the kernel multiplies two longs yet, so nothing reaches
it; the first one that does will fail at link time rather than silently.

**A bug reading the code had missed**, found by running it:
`serial_put_decimal` was wrong for every multi-digit value. Its reversal loop
decremented the tail pointer before storing the byte it had saved, so it
wrote one place short, and it then printed from the head pointer the loop had
advanced — `4095` came out as `54`.
