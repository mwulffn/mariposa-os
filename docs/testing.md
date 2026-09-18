# Testing

Two tiers. The fast one needs no Amiga at all.

| Tier | What it covers | Cost | Command |
|------|----------------|------|---------|
| Headless CPU | ROM routines, as real 68000 code | ~40ms for the whole suite | `make test` |
| FS-UAE | Boot path, real hardware behaviour | seconds, needs a display | `./debug.py`, `test_*.py` |

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
- **Stubs** that exist only to stop routines hanging: CIA-A/B, Zorro
  autoconfig space (reports no card), Gayle IDE (reports no drive).

One deliberate deviation from hardware: the model clears RBF when the guest
reads `SERDATR` as a word, whereas Paula needs an `INTREQ` write to ack.
Without it `serial_get_char`, which never acks, would spin forever. A test of
the ack path itself has to check `INTREQ` directly.

### Adding IDE and FAT16 coverage

`stub_region()` in `harness.c` is the seam. Back the Gayle range with a real
ATA register model over a disk image file and `ide.s`, `partition.s` and
`filesystem.s` become testable headlessly, with no `boot.hdf` and no emulator.

## What the suite found

Building it turned up a bug that reading the code had missed:
`serial_put_decimal` is wrong for every multi-digit value. Its reversal loop
decrements the tail pointer before storing the saved byte, so it writes to the
wrong index, and it then prints from the advanced head pointer rather than the
buffer start — `4095` comes out as `54`. It is currently uncalled, so the bug
is latent.

The other xfails are in `sprintf.s`, and the sharpest one is that the
documented format syntax is not the implemented one. The parser wants the size
modifier *before* the specifier (`%.lx`), but almost every caller writes it
after (`%x.l`, `%x.b`). Those print a full longword followed by the modifier as
literal text — so the values are right and the argument pointer stays in step,
but the output is wrong. Only `memory.s` uses the form the parser implements.
