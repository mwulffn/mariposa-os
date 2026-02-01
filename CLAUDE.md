# Amiga ROM/OS Project

A bare-metal operating system for Amiga 500 (OCS, 68000, 512KB chip + 1MB fast RAM).

## Build

```bash
make          # Build ROM (outputs to build/kick.rom)
make run      # Run in FS-UAE
make run-direct   # Direct FS-UAE launch (if 'open' doesn't work)
```

Requires: vasmm68k_mot (VASM with Motorola syntax), FS-UAE

Assembler flags: `-Fbin -m68000 -no-opt -I$(SRCDIR)`

## Project Structure

```
src/rom/
  bootstrap.s   - ROM entry point, memory detection, enters debugger
  debug.s       - Panic handler with register dump
  serial.s      - Serial port I/O (input and output)
  debugger.s    - Interactive debugger (~630 lines)
  hardware.i    - Hardware definitions
  memory.s      - Memory detection and management
docs/
  rom_design.md - ROM architecture
  debugger.md   - Debugger guide
build/
  kick.rom      - Compiled ROM (256KB)
debug.py        - Interactive debugger launcher
test_*.py       - Test scripts
```

## Documentation

- `docs/rom_design.md` - ROM architecture and design
- `docs/debugger.md` - Interactive debugger guide


## Interactive Debugger

The ROM boots directly into an interactive debugger accessible via serial port.

**Quick Start:**
```bash
./debug.py          # Launches FS-UAE and connects automatically
```

**Commands:** `r` (registers), `m` (memory), `g` (go), `?` (help)

**Full documentation:** See `docs/debugger.md`

**Testing:**
```bash
./test_comprehensive.py    # Full test suite (12 tests)

# Automated testing with debug.py (accepts stdin, prints to stdout):
echo -e "r\nq\n" | ./debug.py 2>&1              # Dump registers and quit
echo -e "m 00200000 20\nq\n" | ./debug.py 2>&1  # Dump memory and quit
echo -e "r\nm fc0000 10\nq\n" | ./debug.py 2>&1 # Multiple commands
```

## Next Steps

- Keyboard input (CIA-A)
- FAT16 filesystem (read-only)
- Load SYSTEM.BIN from disk
