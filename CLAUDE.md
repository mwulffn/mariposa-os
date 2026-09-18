# Amiga ROM/OS Project

A bare-metal operating system for Amiga 500 (OCS, 68000, 512KB chip + 1MB fast RAM).

## Installation

**First time setup:** Install build tools before building:

```bash
# Linux
./install-tools-linux.sh

# macOS
./install-tools-macos.sh
```

After installation, add to your shell config (`~/.bashrc` or `~/.zshrc`):

```bash
export PATH="$HOME/.local/bin:$PATH"
export VBCC="$HOME/.vbcc"
```

**Full installation guide:** See `docs/installation.md`

## Build

```bash
make          # Build both ROM and kernel
make rom      # Build ROM only (outputs to src/rom/build/kick.rom)
make kernel   # Build kernel only (outputs to src/kernel/build/SYSTEM.BIN)
make deploy   # Build kernel and copy to hard drive image
make run      # Build all, deploy kernel, and run in FS-UAE
make clean    # Clean all build artifacts
```

Requires: vasmm68k_mot (VASM with Motorola syntax), Python 3 (the ROM build
runs `tests/mksym.py` to emit a symbol table), FS-UAE, vbcc (for kernel),
mtools (for deployment). `make test` additionally needs a host C compiler and,
on first run, git to fetch Musashi.

**ROM assembler flags:** `-Fbin -m68000 -no-opt`

**Kernel deployment:** The `deploy` target copies `SYSTEM.BIN` to the FAT16 filesystem on `harddrives/boot.hdf` using mtools. The `run` target automatically triggers deployment.

**Switching configs:** Edit `CONFIG = configs/a600.fs-uae` in root Makefile to use a different config (e.g., `configs/a500.fs-uae`)

**Standalone builds:**
```bash
cd src/rom && make      # Build ROM independently
cd src/kernel && make   # Build kernel independently
```

**Containerised build (no host toolchain needed):**
```bash
make docker-build       # Build ROM + kernel inside a container
```
Builds vasm/vbcc/vlink from upstream source in an image and runs these
Makefiles inside it. FS-UAE is not included, so `make run` and `./debug.py`
still need a local install. See `docs/docker.md`.

## Project Structure

```
Makefile                      - Build orchestrator (rom, kernel, test, docker)
src/rom/                      - 256KB ROM, pure 68000 assembly
  bootstrap.s                 - Entry point, hardware init, vectors, boot sequence
  panic.s                     - Panic handler, register dump, exception entry points
  math.s                      - 32-bit multiply and divide-by-10 (68000 has neither)
  autoconfig.s                - Zorro II expansion autoconfig
  memory.s                    - Memory detection, map table, map printing
  serial.s                    - Serial port I/O (polled)
  sprintf.s                   - Formatted output (see docs/rom/sprintf_api.md)
  debugger.s                  - Interactive serial debugger
  ide.s                       - IDE/ATA sector read (Gayle)
  partition.s                 - Rigid Disk Block and partition parsing
  filesystem.s                - FAT16 read, loads SYSTEM.BIN
  hardware.i                  - Hardware definitions and the low-memory map
  build/kick.rom              - Compiled ROM (256KB)
  build/kick.sym              - Symbol table for the tests, from vasm's listing
src/kernel/
  crt0.s                      - Startup stub, receives control from the ROM
  libsup.s                    - 32-bit divide/modulo helpers vbcc calls
  kernel.c                    - Kernel entry point
  mem.c                       - Bump allocator
  serial.c / kprintf.c        - Polled serial and kernel printf
  kernel.ld                   - Linker script, links at $200000
  build/SYSTEM.BIN            - Compiled kernel binary
tests/                        - Headless 68000 test harness (see docs/testing.md)
  harness.{c,h}               - Machine model, symbol lookup, call/run, disk model
  protocol.{c,h}              - The ### result protocol and exit codes
  test_math.c                 - src/rom/math.s
  test_libsup.c               - src/kernel/libsup.s
  test_format.c               - sprintf.s and the serial formatting helpers
  test_vectors.c              - ROM header, vector table, panic frame decoding
  test_disk.c                 - ide.s, partition.s, filesystem.s
  mksym.py                    - vasm listing -> flat symbol table
  mkdisk.py                   - Generates the RDB + FAT16 test disk images
docker/Dockerfile             - Build image, toolchain from upstream source
docs/                         - Architecture and design (see below)
configs/                      - FS-UAE configurations (a500, a600)
debug.py                      - Interactive debugger launcher
test_*.py                     - FS-UAE integration scripts
```

## Testing

```bash
make test                      # headless, 127 tests, ~0.3s, no emulator needed
make test FILTER=rom.panic     # narrow to one group while iterating
```

Runs real 68000 code under a CPU simulator (Musashi) with no emulator, no
display and no serial port. Results use the `###` line protocol and the exit
code is the verdict: 0 pass, 1 test failed, 2 harness error.

| Suite | Covers |
|-------|--------|
| `math.*` | `src/rom/math.s` |
| `libsup.*` | `src/kernel/libsup.s`, assembled standalone - no C compiler needed |
| `format.*` | `sprintf.s`, `serial_put_*`, `parse_hex` |
| `rom.*` | ROM header, exception vector table, panic frame decoding |
| `disk.*` | `ide.s`, `partition.s`, `filesystem.s` against a generated RDB + FAT16 image |

Prefer this tier for anything that is pure logic. Use FS-UAE only for
behaviour that needs real hardware. Nothing here proves the ROM boots.

**Full guide:** See `docs/testing.md`

## Documentation

Decisions about architecture and design can be found int the 'docs' directory. Read them carefully when implementing new features.

## Interactive Debugger

The ROM boots directly into a small fast-ram based kernel

**Quick Start:**
```bash
./debug.py          # Launches FS-UAE and connects automatically
```

**Commands:** `r` (registers), `m` (memory), `g` (go), `?` (help)

**Full documentation:** See `docs/rom/debugger.md`

**Testing:**
```bash

# debug.py accepts stdin and prints to stdout. 'q' is handled by debug.py
# itself, not by the guest debugger.
echo -e "r\nq\n" | ./debug.py 2>&1               # Dump registers and quit
echo -e "m 200000\nq\n" | ./debug.py 2>&1        # Dump 16 bytes at $200000
echo -e "r\nm fc0000\nq\n" | ./debug.py 2>&1    # Multiple commands
```

> **`m <addr> <hex>` writes, it does not dump.** A second argument is the
> value to store, auto-sized by digit count (1-2 byte, 3-4 word, 5-8 long).
> `m 200000 20` stores $20 at $200000. To dump, pass the address only; the
> dump length is fixed at 16 bytes.

Testing like this is only possible if kernel has crashed to debugger, or debugger has been invoked by the code. Invoking the debugger for testing is a good thing.


## Next Steps

Done since this list was written: FAT16 read-only, and loading SYSTEM.BIN
from disk. Both are covered by the `disk.*` tests.

**Next up**

- Interrupts. Nothing enables them today: `bootstrap.s` clears INTENA
  including the master bit and never writes it again, and sets SR to $2700
  immediately before jumping to the kernel, which never lowers it. `cpu.s`
  from `docs/interrupt_control_design.md` is unwritten. Everything below
  waits on this.
- Keyboard input (CIA-A), level 2 PORTS interrupt. Needs the handshake pulse.
- Real memory allocator: free list with coalescing, per `docs/mem_design.md`.
  `mem.c` is a bump allocator with no free.
- Interrupt-driven serial with a ring buffer, per `docs/serial_design.md`.
  Both ROM and kernel serial are polled today.
- Block I/O and FAT16 in the kernel. The ROM's copies are boot-time only, so
  once the kernel is running it cannot read a disk at all.
- Tasks and a scheduler, once interrupts and the allocator are in place.
- Debugger: breakpoints, single-step, disassembly (see `docs/rom_design.md`).
- A tiling workspace: copper-banded screens, blitter text, focus routing.

**Known issues**

- The ROM does not tell the kernel where it booted from. The handoff passes
  only A0 (memory map) and A1 (panic vector); the partition LBA and size are
  computed and then dropped.
- The memory map hands the kernel's own image out as free fast RAM. `mem.c`
  works around it via `_end`; the table itself is still wrong.
- `load_partition` reads the partition block from a hardcoded LBA 1 and
  ignores `RDB_PARTLIST`. `PART_NEXT` is printed but never followed, so only
  the first partition is reachable.
- The debugger's `g` does not restore A7 - it RTEs onto the debugger's own
  stack. `DBG_STACK` is defined, unused, and at an odd address.
- `Sprintf` cannot be called directly; only via `SerialPrintf`. See
  `docs/rom/sprintf_api.md`.
- A fresh clone cannot `make run`: nothing creates `harddrives/boot.hdf`.
- `src/kernel/kernel.asm` is a stale vbcc intermediate committed by accident;
  the build now generates it under `build/`.
