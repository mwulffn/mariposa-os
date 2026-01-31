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
  rom/
    bootstrap.s   - ROM entry point, basic display setup (includes debug.s, serial.s)
    debug.s       - Panic handler with register dump + 8x8 bitmap font (chars 32-126)
    serial.s      - Serial port I/O for real-time debugging output
    hardware.i    - Hardware register definitions (custom chip registers, CIA, serial, etc.)
  kernel/
build/
  rom.bin       - Compiled ROM image (256KB)
Makefile        - Build system
a500.fs-uae     - FS-UAE emulator config (serial port on TCP 5555)
serial_reader.py - Python tool to capture serial output
test_serial.sh   - Automated serial debugging test
```

## Architecture

For documentation see:

  - *ROM* reference 'docs/rom_design.md'


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
