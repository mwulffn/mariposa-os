# Interactive Debugger

The ROM boots directly into an interactive debugger accessible via serial port.

## Quick Start

```bash
./debug.py          # Launches FS-UAE and connects automatically
```

That's it! The script handles everything: starting the emulator, connecting to serial, and providing an interactive prompt.

## Commands

| Command | Description | Example |
|---------|-------------|---------|
| `r` | Display all registers | `r` |
| `r <reg> <hex>` | Set register (D0-D7, A0-A7, PC, SR) | `r D0 DEADBEEF` |
| `m <addr>` | Memory dump (16 bytes) | `m FC0000` |
| `m <addr> <hex>` | Write byte to memory | `m 1000 42` |
| `m` | Continue dump from last address | `m` |
| `g` | Continue execution from saved PC | `g` |
| `g <addr>` | Continue from specified address | `g FC1000` |
| `?` | Display help | `?` |

**Notes:**
- Register names are case-insensitive (D0, d0, A5, a5)
- Hex values can use `$` prefix or not ($DEAD, DEAD)
- Type `quit`, `exit`, or press Ctrl-D to exit

## Example Session

```
$ ./debug.py
Starting FS-UAE emulator...
Waiting for emulator to initialize...... OK
Connecting to serial port (localhost:5555)... Connected!

AMAG ROM v0.1
...
Boot success - GREEN SCREEN

AMAG Debugger v0.1

> ?
Commands:
  r              Display all registers
  ...

> r
=== SYSTEM DEBUG ===
D0:$00200000 D1:$00000000 D2:$00000000 D3:$00000000
D4:$00000000 D5:$00000000 D6:$00000000 D7:$00000000
A0:$00FC0504 A1:$00000000 A2:$00000000 A3:$00000000
A4:$00000000 A5:$00000000 A6:$00DFF000 A7:$00003FFC
PC:$00FC1256 SR:$2700

> r D0 DEADBEEF
OK

> m 0
$00000000: 00 00 00 00 00 00 00 00 00 FC 01 F8 00 FC 02 02

> m
$00000010: 00 FC 02 0C 00 FC 02 16 00 FC 02 20 00 FC 02 2A

> m FC0000
$00FC0000: 00 00 3F FC 00 FC 00 10 41 4D 41 47 00 01 00 00
                                    A  M  A  G  (magic)

> quit
Exiting debugger...
```

## Manual Method (Two Terminals)

If you prefer not to use `debug.py`:

**Terminal 1:** Connect to serial
```bash
nc localhost 5555
```

**Terminal 2:** Start emulator
```bash
make run
```

Then type commands in Terminal 1.

## Common Use Cases

### Examine Exception Vectors
```
> m 0        # Reset vectors
> m 8        # Bus error handler
> m C        # Address error handler
```

### Check ROM Header
```
> m FC0000   # Should show "AMAG" magic at offset 8
```

### Modify and Test Register
```
> r D0 12345678
> r          # Verify D0 = $12345678
```

### Continue Execution
```
> r PC FC1000   # Set new PC
> g             # Continue from FC1000
```

## Troubleshooting

**"ROM not found at build/kick.rom"**
```bash
make           # Build ROM first
./debug.py
```

**"Failed to connect to serial port"**
- Kill existing FS-UAE: `killall fs-uae`
- Check port not in use: `lsof -i :5555`
- Verify `a500.fs-uae` has `serial_port = tcp://127.0.0.1:5555/wait`

**Script hangs**
- Press Ctrl-C multiple times
- Kill manually: `killall fs-uae`

## Technical Details

**Architecture:**
- Boot → DebuggerEntry → DebuggerMain (command loop)
- Exception handlers → Panic → DebuggerMain
- Continue command uses RTE to restore full CPU state

**Memory Layout:**
- $000400: Saved registers (D0-D7, A0-A7, PC, SR)
- $000850: Command buffer (128 bytes)
- $0008D0: Buffer index
- $0008D4: Last memory address

**Serial I/O:**
- Baud rate: 9600 (SERPER = $0170)
- RBF (Receive Buffer Full): SERDATR bit 14
- TBE (Transmit Buffer Empty): SERDATR bit 13
- Polling-based (no interrupts)

**Implementation:**
- `src/rom/debugger.s` - Main debugger (~630 lines)
- `src/rom/serial.s` - Serial I/O (input/output)
- `debug.py` - Convenience launcher

## Testing

Automated test suite:
```bash
./test_comprehensive.py    # 12 tests covering all commands
./test_debugger_verify.py  # Verify register modification
./test_debugger.py         # Basic command test
./test_serial.sh           # Serial output test
```

All tests should pass with no errors.

## Limitations

- Serial input only (no keyboard support)
- No bus error protection on memory access
- Single-byte memory writes only
- No breakpoints or single-step
- No command history (except backspace)

## Files

- `debug.py` - Interactive launcher (recommended)
- `src/rom/debugger.s` - Debugger implementation
- `docs/debugger.md` - This file
