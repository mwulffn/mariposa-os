# ROM Design Document

## Target Hardware

- **CPU:** 68000 @ 7.14MHz
- **Chipset:** ECS (Enhanced Chip Set)
- **Chip RAM:** 1MB
- **Fast RAM:** 8MB
- **Storage:** IDE hard drive
- **Reference machine:** A500+ with expansion

## ROM Location

- **Address:** $FC0000 - $FFFFFF (256KB)
- **Note:** ROM shares bus with chip RAM. Minimize runtime ROM access; copy kernel to fast RAM.

## ROM Responsibilities

1. **Bootstrap**
   - Reset hardware to known state
   - Disable ROM overlay (CIA-A PRA bit 0)
   - Initialize chip registers
   - Set up exception vectors

2. **Memory Detection**
   - Detect chip RAM size (512KB, 1MB, 2MB)
   - Detect fast RAM (location and size)
   - Quick power-on test
   - Support multiple Amiga configurations

3. **Interactive Debugger**
   - Serial I/O (directly banged, 9600 baud default)
   - Keyboard I/O via CIA-A
   - Register display/modification
   - Memory examine/modify
   - Breakpoints
   - Single stepping
   - Disassembly
   - Console with built-in 8x8 font

4. **Boot Loader**
   - IDE driver (read-only sufficient)
   - FAT16 filesystem (read-only)
   - Load SYSTEM.BIN from disk
   - Relocate kernel to fast RAM
   - Transfer control to kernel

5. **Exception Handlers**
   - Bus error
   - Address error
   - Illegal instruction
   - Divide by zero
   - CHK, TRAPV
   - Privilege violation
   - All handlers → debugger with context

6. **ROM Services**
   - Panic: dump registers, enter debugger
   - SerPutc/SerPuts: serial output
   - ConPrint: console output
   - Version/ID query

## Memory Map (Low Chip RAM Reserved for ROM)

```
$00000 - $003FF   Exception vectors (1KB)
$00400 - $0044F   Register dump area (80 bytes)
$00450 - $0084F   Debugger stack (1KB, grows down from $0084F)
$00850 - $008CF   Debugger command buffer (128 bytes)
$008D0 - $0094F   Reserved (128 bytes)
$00950 - $00A4F   Copper list for debugger (256 bytes)
$00A50 - $0324F   Debug display bitplane (10KB, 320x256x1)
$03250 - $033FF   Memory map table (432 bytes, up to 36 entries)
$03400 - $03FFF   Reserved for expansion (~3KB)
$04000 - $FFFFF   Kernel-managed chip RAM (~1008KB)
```

## Exception Handling

On any exception:

1. Store SP to fixed address immediately
2. Switch to debugger stack ($0084F)
3. Save all registers to dump area
4. Retrieve PC/SR from old stack (pushed by CPU)
5. For bus/address errors: save fault address
6. Initialize debugger display
7. Enter interactive debugger

## Boot Sequence

```
1. Hardware init
   - Disable interrupts (SR = $2700)
   - Set up debugger stack
   - Disable all DMA
   - Disable ROM overlay
   - Initialize CIAs
   
2. Exception vectors
   - Point all vectors to ROM handlers
   
3. Memory detection
   - Size chip RAM (512KB, 1MB, 2MB) with mirror detection
   - Size fast RAM at $200000
   - Quick test (optional, skippable)
   - Build memory map table at $3250
   
4. Serial init
   - Configure for 9600 baud
   - Print ROM version banner
   
5. Load kernel
   - Init IDE
   - Mount FAT16
   - Find SYSTEM.BIN
   - Load to $200000 (fast RAM)
   
6. Transfer to kernel
   - A0 = pointer to memory map ($3250)
   - A1 = ROM debugger entry point
   - SSP = top of fast RAM ($A00000 for 8MB at $200000)
   - Jump to $200000
   
On failure at any step → enter debugger with error message
```

## Debugger Commands (Planned)

```
r              - Display registers
r D0 12345678  - Set register
m <addr>       - Memory dump
m <addr> <val> - Memory modify
d <addr>       - Disassemble
g              - Go (continue)
g <addr>       - Go from address
s              - Single step
b <addr>       - Set breakpoint
bc <n>         - Clear breakpoint
bl             - List breakpoints
reset          - Hard reset
?              - Help
```

## ROM Identification

```
Offset 0:      Stack pointer (for reset vector)
Offset 4:      Entry point (for reset vector)
Offset 8:      Magic number ($414D4147 = 'AMAG')
Offset 12:     ROM version (word)
Offset 14:     ROM flags (word)
```

## IDE Interface

- Memory mapped registers (depends on hardware config)
- 28-bit LBA addressing
- PIO mode (no DMA needed for boot)
- Read sectors only (write not needed in ROM)
- Timeout: ~2 seconds per operation. On timeout → enter debugger with error.

## FAT16 Support

- Read-only
- Parse boot sector (BPB)
- Navigate FAT table
- Read root directory
- Follow cluster chains
- 8.3 filenames sufficient

## Kernel File Format

For initial development, SYSTEM.BIN is a raw binary:

- Loaded to fixed address: $200000 (start of fast RAM)
- Entry point: $200000 (first instruction)
- No header, no relocation
- Maximum size: 64KB initially

Later: add header with magic, entry offset, relocation tables.

## Kernel Entry Conditions

When ROM jumps to kernel, the following state is guaranteed:

| Register | Value |
|----------|-------|
| A0 | Pointer to memory map ($3250) |
| A1 | ROM debugger entry point (Panic) |
| A7/SSP | Top of fast RAM ($A00000 for 8MB) |
| SR | Supervisor mode, interrupts disabled ($2700) |
| PC | $200000 |

Hardware state:
- All DMA disabled
- All interrupts disabled
- ROM overlay off (chip RAM visible at $0)
- CIAs in known state
- Display off

Kernel can call ROM debugger at any time via `jsr (a1)`.

## Debugger Display

- PAL timing: 320×256
- 1 bitplane (2 colors)
- Background: $008 (dark blue)
- Foreground: $FFF (white)
- Font: 8×8 pixels, 40 columns × 32 rows

## Error Handling

ROM displays human-readable errors before entering debugger:

- IDE initialization failures
- Disk read timeouts
- SYSTEM.BIN not found
- File read errors
- Memory test failures (fast RAM)
- Invalid kernel (future: bad magic/checksum)

All errors halt at debugger prompt — never silent hang, never auto-reboot.

**Exception: Chip RAM failure**

If chip RAM tests bad, debugger cannot function (no display, no copper, no workspace). In this case:
- Set background color to bright yellow ($FF0)
- Halt (infinite loop)

This is the only case where ROM halts without debugger.

## Serial Protocol

- 9600 baud default (configurable via SERPER)
- 8N1 format
- No flow control
- Used for:
  - Debug output during boot
  - Interactive debugger I/O
  - Kernel debug logging (optional)

## Design Principles

1. **ROM is bootstrap, not runtime** — Load kernel to fast RAM, run from there
2. **Debugger is essential** — Invest in good debugging; it accelerates everything else
3. **Fail safe** — Any error → debugger, never silent hang
4. **Trust nothing on crash** — Dedicated stack, fixed memory locations
5. **Keep it simple** — IDE not SCSI, FAT16 not FFS, polling not interrupts (for ROM)
6. **68000 has 24-bit address bus** — Maximum address is $FFFFFF. Do not probe above this.

## Memory Map Handoff

ROM detects memory and passes a map to the kernel at $3250. All entries are longword aligned.

**Entry format (12 bytes each):**

```
Offset  Size  Field
0       4     base    Start address
4       4     size    Length in bytes
8       2     type    Memory type
10      2     flags   Attributes
```

**Memory types:**

| Value | Type |
|-------|------|
| 0 | End of list (terminator) |
| 1 | Chip RAM |
| 2 | Fast RAM ($200000+) |
| 5 | ROM |
| 6 | Reserved / system |

**Flags (bit field):**

| Bit | Meaning |
|-----|---------|
| 0 | Memory tested OK |
| 1 | DMA capable (custom chips can access) |

**Example for A500+ with 1MB chip + 8MB fast:**

```
Address   Contents
$03250    $00004000   ; base: chip RAM starts at $4000 (after ROM workspace)
$03254    $000FC000   ; size: 1008KB ($100000 - $4000)
$03258    $0001       ; type: chip RAM
$0325A    $0003       ; flags: tested + DMA capable

$0325C    $00200000   ; base: fast RAM at $200000
$03260    $00800000   ; size: 8MB
$03264    $0002       ; type: fast RAM
$03266    $0001       ; flags: tested

$03268    $00000000   ; base: 0 = end of list
$0326C    $00000000   ; size: 0
$03270    $0000       ; type: 0 (terminator)
$03272    $0000       ; flags: 0
```