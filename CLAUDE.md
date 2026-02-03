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

Requires: vasmm68k_mot (VASM with Motorola syntax), FS-UAE, vbcc (for kernel), mtools (for deployment)

**ROM assembler flags:** `-Fbin -m68000 -no-opt`

**Kernel deployment:** The `deploy` target copies `SYSTEM.BIN` to the FAT16 filesystem on `harddrives/boot.hdf` using mtools. The `run` target automatically triggers deployment.

**Switching configs:** Edit `CONFIG = configs/a600.fs-uae` in root Makefile to use a different config (e.g., `configs/a500.fs-uae`)

**Standalone builds:**
```bash
cd src/rom && make      # Build ROM independently
cd src/kernel && make   # Build kernel independently
```

## Project Structure

```
Makefile                      - Main build orchestrator
src/rom/
  Makefile                    - ROM build
  bootstrap.s                 - ROM entry point, memory detection, enters debugger
  debug.s                     - Panic handler with register dump
  serial.s                    - Serial port I/O (input and output)
  debugger.s                  - Interactive debugger (~630 lines)
  hardware.i                  - Hardware definitions
  memory.s                    - Memory detection and management
  build/
    kick.rom                  - Compiled ROM (256KB)
src/kernel/
  Makefile                    - Kernel build
  kernel.c                    - Kernel entry point
  build/SYSTEM.BIN            - Compiled kernel binary
docs/
  rom_design.md               - ROM architecture
  debugger.md                 - Debugger guide
configs/
  a500.fs-uae                 - Amiga 500 FS-UAE configuration
  a600.fs-uae                 - Amiga 600 FS-UAE configuration
debug.py                      - Interactive debugger launcher
test_*.py                     - Test scripts
```

## Documentation

Decisions about architecture and design can be found int the 'docs' directory. Read them carefully when implementing new features.

## Interactive Debugger

The ROM boots directly into a small fast-ram based kernel

**Quick Start:**
```bash
./debug.py          # Launches FS-UAE and connects automatically
```

**Commands:** `r` (registers), `m` (memory), `g` (go), `?` (help)

**Full documentation:** See `docs/debugger.md`

**Testing:**
```bash

# Automated testing with debug.py (accepts stdin, prints to stdout):
echo -e "r\nq\n" | ./debug.py 2>&1              # Dump registers and quit
echo -e "m 00200000 20\nq\n" | ./debug.py 2>&1  # Dump memory and quit
echo -e "r\nm fc0000 10\nq\n" | ./debug.py 2>&1 # Multiple commands
```

Testing like this is only possible if kernel has crashed to debugger, or debugger has been invoked by the code. Invoking the debugger for testing is a good thing.


## Next Steps

- Keyboard input (CIA-A)
- FAT16 filesystem (read-only)
- Load SYSTEM.BIN from disk
