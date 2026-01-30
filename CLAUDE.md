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
src/
  bootstrap.s   - ROM entry point, basic display setup (includes debug.s)
  debug.s       - Panic handler with register dump + 8x8 bitmap font (chars 32-126)
  hardware.i    - Hardware register definitions (custom chip registers, CIA, etc.)
build/
  rom.bin       - Compiled ROM image (256KB)
Makefile        - Build system
a500.fs-uae     - FS-UAE emulator config
```

## Architecture

- ROM at $FC0000 (256KB)
- Screen at $20000 (chip RAM)
- Stack at $7FFFE (top of chip RAM)
- Copperlist at $1F000 (chip RAM)
- Register save area at $1000 (chip RAM, used by Panic)
- Must disable ROM overlay (CIA-A PRA bit 0) before accessing chip RAM at $0

Color scheme:
- Background: $0006 (dark blue)
- Text: $0FFF (white)
- Error title: $0F00 (red)

## Code Style

- VASM Motorola syntax
- Labels: PascalCase for functions, .localLabel for local
- Use `include "hardware.i"` for register definitions
- Call `jsr Panic` to dump registers and halt

## Current State

- Boots to "READY." text, then calls Panic as a test
- Panic handler displays CPU state dump with all registers (D0-D7, A0-A7, SR, PC)
- Registers saved to $1000 before display
- Display: 1 bitplane, 320x256, PAL

## Debug/Testing

The Panic handler provides a CPU state dump for debugging:

```asm
jsr Panic           ; Dump all registers and halt
jsr PanicWithMsg    ; Custom error message (A0 = msg pointer)
```

Features:
- Saves all registers (D0-D7, A0-A7, SR, PC) to $1000
- Displays register values in hex on screen
- Uses 8x8 bitmap font (included in debug.s)
- Red title bar with white text on dark blue background
- Halts system after display

## Next Steps

- Keyboard input (CIA-A)
- FAT16 filesystem (read-only)
- Load SYSTEM.BIN from disk
