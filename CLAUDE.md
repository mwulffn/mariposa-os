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
on first run, git to fetch Musashi; it also runs `make lint`, which uses uv if
present and is skipped if not.

**ROM build:** assembled and compiled to ELF objects, then linked into the
raw 256KB image by `vlink -b rawbin1 -T rom.ld`. Assembler flags
`-Felf -m68000 -no-opt`, vbcc `-cpu=68000 -O=1`. `bootstrap.s` still includes
the other `.s` files, so the assembly is one object; C sources are separate
ones. See `docs/rom_scope_design.md`.

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
  autoconfig.c                - Zorro II expansion autoconfig
  autoconfig_glue.s           - Register ABI shim between callers and autoconfig.c
  memory.c                    - Memory detection, map table, map printing
  memory_glue.s               - Register ABI shim between callers and memory.c
  serial.c                    - Serial port I/O (polled, see serial_glue.s)
  serial_glue.s               - Register ABI shim between callers and serial.c
  sprintf.c                   - Formatted output (see docs/rom/sprintf_api.md)
  sprintf_glue.s              - Stack ABI shim between callers and sprintf.c
  rom.ld                      - Linker script, 256KB image at $FC0000
  debugger.s                  - Interactive serial debugger
  ide.c                       - Gayle register map + the boot device table
  ide_glue.s                  - Register ABI shim between callers and ide.c
  rom.h                       - Interface between the ROM's C modules
  cpu_detect.s                - Which 680x0 and FPU, by probing; reported in bootinfo
  disk.c                      - Boot path across the disk: what to read, what to say
  disk_glue.s                 - Register ABI shim between callers and disk.c
  hardware.i                  - Hardware definitions and the low-memory map
  build/kick.rom              - Compiled ROM (256KB)
  build/kick.sym              - Symbol table for the tests, from the vlink map
src/shared/                   - Compiled into BOTH the ROM and the kernel
  amiga_hw.h                  - Custom chip registers and bit definitions
  serial_hw.{c,h}             - Paula UART primitives; no waiting strategy
  ata.{c,h}                   - LBA28 PIO reads; no register addresses
  rdb.{c,h}                   - Rigid Disk Block parsing; returns structs, prints nothing
  fat16.{c,h}                 - Read-only FAT16; returns structs, prints nothing
  blkdev.h                    - Block device handle; seed of a device model
  memmap.{c,h}                - The map the ROM builds and the kernel reads
  bootinfo.h                  - The handoff struct: A0 at kernel entry, versioned
src/kernel/
  crt0.s                      - Startup stub, receives control from the ROM
  cpu.{s,h}                   - SR primitives, CRITICAL_ENTER/EXIT, cpu_idle
  isr.s                       - Minimal vertical-blank ISR; the tests' reference handler.
                                The kernel itself installs switch.s's tick_handler
  vectors.s                   - Stubs that call C: serial TBE ISR, crash flush
  switch.s                    - Context switch: tick handler, isr_exit, TRAP #0 yield
  task.{c,h}                  - Tasks, priorities, sleep, wait queues, exit and reaping
  vector.{c,h}                - cpu_type and vector_set(): the table is at VBR on 68010+
  irq.{c,h}                   - Vector install and INTENA setup
  libsup.s                    - 32-bit divide/modulo helpers vbcc calls
  kernel.c                    - Kernel entry point
  mem.c                       - Free-list allocator: chip best-fit, fast/slow first-fit
  serial.c                    - Ring buffer + TBE interrupt; polled until irq_init
  kprintf.c                   - Kernel printf
  kernel.ld                   - Linker script, links at $200000
  build/SYSTEM.BIN            - Compiled kernel binary
  build/kernel.sym            - Symbol table for the tests, from the vlink map
tests/                        - Headless 68000 test harness (see docs/testing.md)
  harness.{c,h}               - Machine model, symbol lookup, call/run, disk model
  protocol.{c,h}              - The ### result protocol and exit codes
  test_libsup.c               - src/kernel/libsup.s
  test_format.c               - sprintf.c, the ABI shim, and serial formatting
  test_vectors.c              - ROM header, vector table, panic frame decoding
  test_memory.c               - memory.c, memmap.c, the kernel reservation
  test_zorro.c                - autoconfig.c against a modelled Zorro II card
  test_irq.c                  - Paula interrupt registers, autovector dispatch
  test_cpu.c                  - src/kernel/cpu.s and isr.s, loaded as modules
  test_kserial.c              - src/kernel/serial.c, run out of the real SYSTEM.BIN
  test_boot.c                 - bootinfo layout, and entering the real kernel with it
  test_kmem.c                 - src/kernel/mem.c against hand-built memory maps
  test_task.c                 - The scheduler, every test on a 68000 and a 68020 core
  guest/ktasks.s              - Bodies of the tasks the scheduler tests run
  test_disk.c                 - ata.c/ide.c, rdb.c, fat16.c, disk.c
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
make test                      # headless, 251 tests, ~0.3s, no emulator needed
make test FILTER=rom.panic     # narrow to one group while iterating
```

Runs real 68000 code under a CPU simulator (Musashi) with no emulator, no
display and no serial port. Results use the `###` line protocol and the exit
code is the verdict: 0 pass, 1 test failed, 2 harness error.

| Suite | Covers |
|-------|--------|
| `libsup.*` | `src/kernel/libsup.s`, assembled standalone - no C compiler needed |
| `format.*` | `sprintf.c`, `sprintf_glue.s`, `serial_put_*`, `parse_hex` |
| `rom.*` | ROM header, IACK vector table, exception vector table, panic frame decoding |
| `mem.*` | `memory.c`, `memmap.c`: detection, the map, the kernel reservation |
| `zorro.*` | `autoconfig.c` against a modelled Zorro II card |
| `irq.*` | INTENA/INTREQ, interrupt levels, autovector dispatch, UART TBE |
| `cpu.*` | `src/kernel/cpu.s` SR primitives, `cpu_idle`, and `isr.s` vertical-blank handler |
| `boot.*` | CPU detection on five cores; `bootinfo.h` layout as an ABI; the real kernel entered with good, bad and short handoffs |
| `kmem.*` | `src/kernel/mem.c`: fit policy, coalescing, bad frees, ownership, `mem_check`, random churn |
| `task.*` | `task.c` + `switch.s`: yield, preemption, priorities, sleep, wait queues, exit, stack overflow - each on two CPU cores |
| `kser.*` | `src/kernel/serial.c` + `vectors.s`: ring buffer, TBE ISR, full ring, crash flush |
| `disk.*` | `ata.c`, `ide.c`, `rdb.c`, `fat16.c` against a generated RDB + FAT16 image |

Prefer this tier for anything that is pure logic. Use FS-UAE only for
behaviour that needs real hardware. Nothing here proves the ROM boots.

**Full guide:** See `docs/testing.md`

### Python helper scripts

`make test` runs `make lint` first, so a lint error fails the suite before a
single test runs. uv is used only to pin ruff:

```bash
make lint     # uv run ruff check .   (skipped with a notice if uv is absent)
make fmt      # uv run ruff format .
uv sync       # install the dev group after a fresh clone
```

Rules are pinned in `pyproject.toml` to `E4,E7,E9,F,I` - real bugs and import
order, no stylistic rewrites. Add dependencies with `uv add --dev <pkg>`, never
by editing `pyproject.toml` by hand.

**Constraint: uv must never end up on the build path.** `tests/mksym.py` and
`tests/mkdisk.py` are invoked as bare `python3` from `src/rom/Makefile` and
`tests/Makefile`, and `docker/Dockerfile` carries `python3` and nothing else.
Keep both scripts stdlib-only, and keep `uv run` out of every Makefile rule
except `lint` and `fmt` - which is why `lint` skips rather than fails when uv
is missing, so `make docker-make DOCKER_TARGET=test` still works.

`tests/mksym.py` parses two different vasm listing formats: the pre-2.0
`NAME LAB (0xADDR)` form and the 2.0+ `NAME A:ADDR` / `E:VALUE` /
`SS:OFFSET EXP` forms. vasm changed this between 1.9 and 2.0, which silently
broke the whole harness at the symbol-table step.

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
from disk. Both are covered by the `disk.*` tests. The memory map's flags
are also settled: the kernel includes `src/shared/memmap.h` instead of
restating the struct and the flag bits, and `mem.*` pins what the ROM
writes. Fixing those turned up a live stack bug: `bootstrap.s` picked the
first RESERVED entry at or above $200000 as the kernel stack, which became
the loaded kernel image itself once `reserve_kernel_image` started carving
it out, so the kernel ran with ~500 bytes of stack on top of its own code.
`rom_kernel_stack_top` takes the highest reserved region in fast RAM
instead, and `mem.stack_*` pins it.

**Next up**

- Interrupts are armed: `kernel_main` calls `irq_init` and
  `cpu_int_enable`. `src/kernel/cpu.s` has the SR wrappers and `cpu_idle()`
  (STOP), `cpu.h` the critical-section macros.
- Serial transmit is interrupt-driven per `docs/serial_design.md`, which was
  rewritten to match: the first draft's enable-TBE-on-putc scheme deadlocks
  after the first burst. `make test` now builds the kernel too, because
  `kser.*` runs the driver out of the real `SYSTEM.BIN` (symbols under a
  `kernel:` prefix). A crash flushes the ring before the ROM's panic dump.
- The handoff is `struct bootinfo` (`src/shared/bootinfo.h`) in A0: versioned
  and sized so fields can be appended, carrying the memory map, the boot
  partition's LBA and length, kernel base/size and the stack top. `boot.*`
  enters the real kernel with good, bad and truncated structs.
- The allocator is a free list with coalescing per `docs/mem_design.md`,
  whose status block lists where the code deliberately differs.
- Tasks and a preemptive scheduler per `docs/task_design.md`: kernel
  threads, four priorities, the 50Hz tick, sleep and wait queues. Built to
  run unchanged up to a 68060 - the ROM detects the CPU (`bootinfo` v2),
  vectors are installed through `vector_set()`, and only
  `build_initial_frame` knows an exception frame's layout. Verified under
  FS-UAE with `cpu = 68000` and `cpu = 68020`. `kernel_main` ends in
  `sched_start()`, which turns the boot context into the idle task.
- **Build gotcha:** macOS ships make 3.81, which compares timestamps to the
  second. Two builds inside one second leave a stale object in the link -
  it bit a scripted edit-build-test loop here, not normal use. When
  scripting rebuilds, `make -C src/kernel clean` first. C objects do depend
  on every header now (vbcc has no -MD), so a changed struct in
  `src/shared` rebuilds both the ROM and the kernel.
- Keyboard input (CIA-A), level 2 PORTS interrupt. Needs the handshake pulse.
- Input, now that a task can block waiting for it: keyboard (below) or
  serial receive, an RBF-driven ring at level 5, with a small console task.
- A sleeping mutex, with its first user. Tasks sleeping on a full serial
  ring instead of polling. FPU context (the slot in `struct task` is
  reserved). All listed in `docs/task_design.md`.
- Block I/O and FAT16 in the kernel. The ROM's copies are boot-time only, so
  once the kernel is running it cannot read a disk at all.
- Debugger: breakpoints, single-step, disassembly (see `docs/rom_design.md`).
- A tiling workspace: copper-banded screens, blitter text, focus routing.

**The ROM must end with the IACK vector table**

The last 16 bytes of the ROM are `0018 0019 ... 001F`, as in every Kickstart
(`iack_vector_table` in `bootstrap.s`, pinned at `$FFFFF0` by `rom.ld`).
During interrupt acknowledge the 68000 drives `$FFFFF1 + 2*level`, and UAE's
cycle-exact 68000 takes the byte read there as the vector *number*. With
zeros there every interrupt went through vector 0: PC loaded from `$0`
(contents 0), two `ORI.B #0,D0` executed out of the empty reset vectors, then
ILLEGAL INSTRUCTION at `PC=$00000008` with the interrupt's mask in SR. The
vector table at `$64-$7C` was correct throughout and irrelevant - which is
why this looked like an emulator bug, and was not one. The harness's
`int_ack` now reads the same ROM byte instead of returning Musashi's
autovector constant, so zeroing the table fails `rom.iack_table` plus nine
`irq.*`/`cpu.*` tests. The ROM's back-pointer moved from `$FFFFFC` to
`$FFFFEC` to make room.

**Known issues**

- `load_partition` reads the partition block from a hardcoded LBA 1 and
  ignores `RDB_PARTLIST`. `PART_NEXT` is printed but never followed, so only
  the first partition is reachable. Carried over unchanged from the assembly
  during the C conversion; fixing it needs a test image whose partition list
  is not at block 1.
- `debugger_entry` sets `saved_pc` to itself, so `g` after a deliberate
  break re-enters the debugger instead of returning to whoever broke in.
  It is entered by `jmp` from `bootstrap.s` and by `jsr` through the
  kernel's `rom_panic` pointer, and those two disagree about whether there
  is a return address to resume to. `g` from a real fault is unaffected.
- A fresh clone cannot `make run`: nothing creates `harddrives/boot.hdf`.
- `configure_zorro_ii` advances its slot pointer by $10000 per card. Every
  Zorro II board answers at $E80000 in turn, so a second card would be looked
  for in the wrong place and never found. Harmless today because the space
  above the slot floats and the scan just ends, and because one card is all
  anything here has.
