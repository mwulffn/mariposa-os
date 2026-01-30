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
  bootstrap.s   - ROM entry point, basic display setup (includes debug.s, serial.s)
  debug.s       - Panic handler with register dump + 8x8 bitmap font (chars 32-126)
  serial.s      - Serial port I/O for real-time debugging output
  hardware.i    - Hardware register definitions (custom chip registers, CIA, serial, etc.)
build/
  rom.bin       - Compiled ROM image (256KB)
Makefile        - Build system
a500.fs-uae     - FS-UAE emulator config (serial port on TCP 5555)
serial_reader.py - Python tool to capture serial output
test_serial.sh   - Automated serial debugging test
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

### Panic Handler (On-Screen + Serial Debug)

The Panic handler provides a CPU state dump for debugging on both screen and serial:

```asm
jsr Panic           ; Dump all registers and halt
jsr PanicWithMsg    ; Custom error message (A0 = msg pointer)
```

Features:
- Saves all registers (D0-D7, A0-A7, SR, PC) to $1000
- **Displays on screen:** register values in hex with 8x8 bitmap font
- **Outputs to serial:** formatted register dump sent to serial port
- Red title bar with white text on dark blue background
- Halts system after display

**Serial Output Format:**
```
=== SYSTEM DEBUG ===
D0:$XXXXXXXX D1:$XXXXXXXX D2:$XXXXXXXX D3:$XXXXXXXX
D4:$XXXXXXXX D5:$XXXXXXXX D6:$XXXXXXXX D7:$XXXXXXXX
A0:$XXXXXXXX A1:$XXXXXXXX A2:$XXXXXXXX A3:$XXXXXXXX
A4:$XXXXXXXX A5:$XXXXXXXX A6:$XXXXXXXX A7:$XXXXXXXX
PC:$XXXXXXXX SR:$XXXX
[Custom message if provided]
```

This dual-output approach means panic dumps are captured via serial for logging/analysis while still being visible on screen.

### Serial Port Debugging (Real-Time Output)

Serial port provides real-time debugging output via FS-UAE's TCP serial emulation (9600 baud).

**In Code:**
```asm
    bsr     SerialInit          ; Call once during boot (already done in bootstrap.s)

    ; Send a single character
    move.b  #'A',d0
    bsr     SerialPutChar

    ; Send a string
    lea     MyDebugMsg(pc),a0
    bsr     SerialPutString

MyDebugMsg:
    dc.b    "Debug message here",10,13,0   ; 10,13 = LF,CR for line endings
    even
```

**Manual Testing:**
```bash
# Terminal 1 - Start serial monitor first
nc localhost 5555

# Terminal 2 - Run emulator
make run
```

**Automated Testing (for Claude):**
```bash
./test_serial.sh    # Runs FS-UAE, captures serial output, displays results
```

**How Claude Can Debug:**
1. Add `SerialPutString` calls in the code to output debug messages
2. Build with `make`
3. Run `./test_serial.sh` to capture and view serial output
4. Serial output shows real-time execution flow without stopping the system

**Technical Details:**
- Serial registers: SERDATR ($DFF018), SERDAT ($DFF030), SERPER ($DFF032)
- Polling-based output (no interrupts)
- FS-UAE config: `serial_port = tcp://127.0.0.1:5555/wait`
- Python reader (`serial_reader.py`) provides reliable capture for automated testing

## Next Steps

- Keyboard input (CIA-A)
- FAT16 filesystem (read-only)
- Load SYSTEM.BIN from disk
