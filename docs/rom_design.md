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

2. **Zorro II Autoconfig**
   - Probe $E80000 for expansion cards
   - Configure and relocate memory boards to $200000+
   - Configure I/O boards as needed

3. **Memory Detection**
   - Detect chip RAM size (512KB, 1MB, 2MB)
   - Detect fast RAM (location and size)
   - Quick power-on test
   - Support multiple Amiga configurations

4. **Interactive Debugger**
   - Serial I/O (directly banged, 9600 baud default)
   - Keyboard I/O via CIA-A
   - Register display/modification
   - Memory examine/modify
   - Breakpoints
   - Single stepping
   - Disassembly
   - Console with built-in 8x8 font

5. **Boot Loader**
   - IDE driver (read-only sufficient)
   - FAT16 filesystem (read-only)
   - Load SYSTEM.BIN from disk
   - Relocate kernel to fast RAM
   - Transfer control to kernel

6. **Exception Handlers**
   - Bus error
   - Address error
   - Illegal instruction
   - Divide by zero
   - CHK, TRAPV
   - Privilege violation
   - All handlers â†’ debugger with context

7. **ROM Services**
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
$03400 - $034FF   Sprintf output buffer (256 bytes)
$03500 - $03FFF   Reserved for expansion (~2.75KB)
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
   - Run Zorro II autoconfig (relocate expansion cards)
   - Size chip RAM (512KB, 1MB, 2MB) with mirror detection
   - Size fast RAM at $200000
   - Reserve top 8KB of fast RAM for kernel stack ($9FE000-$9FFFFF)
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
   - SSP = top of kernel stack ($A00000, 8KB reserved at $9FE000-$9FFFFF)
   - Jump to $200000
   
On failure at any step â†’ enter debugger with error message
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

## Zorro II Autoconfig

Expansion cards (including RAM) must be configured before they appear at their final addresses. Cards initially respond at $E80000 and are relocated by ROM.

**Autoconfig space:** $E80000 - $E8007F

**Registers (active low nibbles, read high nibble of each word):**

| Offset | Name | Description |
|--------|------|-------------|
| $00 | er_Type | Board type and size |
| $02 | er_Product | Product number |
| $04 | er_Flags | Configuration flags |
| $08-$0E | er_Manufacturer | 16-bit manufacturer ID |
| $10-$16 | er_SerialNumber | 32-bit serial |
| $40 | ec_Interrupt | Interrupt config (active low) |
| $48 | ec_BaseAddress | Write high byte of base address |
| $4A | ec_BaseAddress | Write low byte (triggers relocation) |
| $4C | ec_Shutup | Write to disable card |

**er_Type ($00) bits:**

| Bits | Meaning |
|------|---------|
| 7-6 | Type: 11=board with size field, 10=board no size |
| 5 | Chained (more boards follow) |
| 4 | ROM vector present |
| 3-0 | Size code |

**Size codes (bits 3-0):**

| Code | Size |
|------|------|
| 0000 | 8 MB |
| 0001 | 64 KB |
| 0010 | 128 KB |
| 0011 | 256 KB |
| 0100 | 512 KB |
| 0101 | 1 MB |
| 0110 | 2 MB |
| 0111 | 4 MB |

**er_Flags ($04) bits:**

| Bits | Meaning |
|------|---------|
| 7 | Memory board (1=memory, 0=I/O) |
| 6 | Boot ROM present |
| 5 | Can't be shutup |
| 4 | Reserved |
| 3-0 | Reserved |

**Autoconfig algorithm:**

```
1. Check er_Type at $E80000
2. If $FF or $00 â†’ no more cards, done
3. Read size from er_Type bits 3-0
4. Allocate base address (start at $200000 for RAM)
5. Write base address high byte to $E80048
6. Write base address low byte to $E8004A (triggers relocation)
7. Card is now at new address
8. Increment base address by card size
9. Go to step 1 (next card appears at $E80000)
```

**Notes:**
- Autoconfig nibbles are active-low (inverted)
- Read high nibble: `move.b (a0),d0; lsr.b #4,d0; eor.b #$F,d0`
- Memory boards (er_Flags bit 7) should be assigned $200000+
- I/O boards typically get $E90000+

## IDE Interface

- Memory mapped registers (depends on hardware config)
- 28-bit LBA addressing
- PIO mode (no DMA needed for boot)
- Read sectors only (write not needed in ROM)
- Timeout: ~2 seconds per operation. On timeout â†’ enter debugger with error.

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
| A7/SSP | Top of kernel stack ($A00000, 8KB reserved at $9FE000) |
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

- PAL timing: 320Ã—256
- 1 bitplane (2 colors)
- Background: $008 (dark blue)
- Foreground: $FFF (white)
- Font: 8Ã—8 pixels, 40 columns Ã— 32 rows

## Error Handling

ROM displays human-readable errors before entering debugger:

- IDE initialization failures
- Disk read timeouts
- SYSTEM.BIN not found
- File read errors
- Memory test failures (fast RAM)
- Invalid kernel (future: bad magic/checksum)

All errors halt at debugger prompt â€” never silent hang, never auto-reboot.

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

1. **ROM is bootstrap, not runtime** â€” Load kernel to fast RAM, run from there
2. **Debugger is essential** â€” Invest in good debugging; it accelerates everything else
3. **Fail safe** â€” Any error â†’ debugger, never silent hang
4. **Trust nothing on crash** â€” Dedicated stack, fixed memory locations
5. **Keep it simple** â€” IDE not SCSI, FAT16 not FFS, polling not interrupts (for ROM)
6. **68000 has 24-bit address bus** â€” Maximum address is $FFFFFF. Do not probe above this.

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
$03260    $007FE000   ; size: 8184KB ($800000 - $2000, kernel stack excluded)
$03264    $0002       ; type: fast RAM
$03266    $0001       ; flags: tested

$03268    $009FE000   ; base: kernel stack
$0326C    $00002000   ; size: 8KB
$03270    $0006       ; type: reserved
$03272    $0001       ; flags: tested

$03274    $00000000   ; base: 0 = end of list
$03278    $00000000   ; size: 0
$0327C    $0000       ; type: 0 (terminator)
$0327E    $0000       ; flags: 0
```