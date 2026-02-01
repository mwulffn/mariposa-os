# A600/A1200 IDE Controller - Bare-Metal Reference

## Overview

This document describes the low-level hardware interface for accessing the built-in IDE controller on Amiga 600 and Amiga 1200 computers. This information is intended for developers writing bare-metal ROM code or custom operating systems that need direct hardware access without AmigaOS.

The IDE interface on A600/A1200 is controlled by the GAYLE chip, which provides memory-mapped access to standard ATA/IDE registers with Amiga-specific quirks.

## Memory Map

### IDE Controller Base Address

The A600/A1200 IDE controller has a **fixed base address** - no Autoconfig scanning is required.

```
Base Address: 0xDA0000
```

### IDE Register Addresses

**Critical:** Register spacing is **0x100 bytes** (256 bytes), NOT the standard PC IDE spacing of 1 byte!

```
Register            Address     Access  Description
--------            -------     ------  -----------
IDE_DATA            0xDA0000    R/W     Data port (16-bit)
IDE_ERROR           0xDA0100    R       Error register
IDE_FEATURE         0xDA0100    W       Features register (same address)
IDE_NSECTOR         0xDA0200    R/W     Sector count
IDE_SECTOR          0xDA0300    R/W     Sector number / LBA bits 0-7
IDE_LCYL            0xDA0400    R/W     Cylinder low / LBA bits 8-15
IDE_HCYL            0xDA0500    R/W     Cylinder high / LBA bits 16-23
IDE_SELECT          0xDA0600    R/W     Drive/head select / LBA bits 24-27
IDE_STATUS          0xDA0700    R       Status register
IDE_COMMAND         0xDA0700    W       Command register (same address)
```

### GAYLE Control Registers

```
Register            Address     Access  Description
--------            -------     ------  -----------
GAYLE_CS            0xDA8000    R/W     Status/control register
GAYLE_IRQ           0xDA9000    R/W     Interrupt status/clear
GAYLE_INT           0xDAA000    R/W     Interrupt enable
GAYLE_CFG           0xDAB000    R/W     Configuration (timing/voltage)
```

### Secondary Drive Support

If an IDE splitter is enabled (for accessing a second drive on the same channel):

```
Secondary offset: Add 0x400 to primary register addresses

Examples:
IDE_DATA (secondary)   = 0xDA0400
IDE_STATUS (secondary) = 0xDA0B00
```

## Register Bit Definitions

### IDE_STATUS Register (0xDA0700, Read)

```
Bit 7 - BSY   (Busy)               Controller is busy processing
Bit 6 - DRDY  (Drive Ready)        Drive is ready for commands
Bit 5 - DF    (Drive Fault)        Drive fault condition
Bit 4 - DSC   (Seek Complete)      Seek operation completed
Bit 3 - DRQ   (Data Request)       Data transfer requested
Bit 2 - CORR  (Corrected)          Data was corrected
Bit 1 - IDX   (Index)              Index pulse
Bit 0 - ERR   (Error)              Error occurred
```

### IDE_ERROR Register (0xDA0100, Read)

```
Bit 7 - BBK   (Bad Block)          Bad block detected
Bit 6 - UNC   (Uncorrectable)      Uncorrectable data error
Bit 5 - MC    (Media Changed)      Media changed
Bit 4 - IDNF  (ID Not Found)       ID mark not found
Bit 3 - MCR   (Media Change Req)   Media change requested
Bit 2 - ABRT  (Aborted)            Command aborted
Bit 1 - NM    (No Media)           No media present
Bit 0 - (unused)
```

### IDE_SELECT Register (0xDA0600, Read/Write)

```
Format: 101d hhhh

Bits 7-5: Always 101 (0xA0 base value)
Bit 4:    Drive select (0 = master, 1 = slave)
Bits 3-0: Head number (CHS mode) or LBA bits 24-27 (LBA mode)

For LBA mode: Set to 0xE0 for master, 0xF0 for slave
```

### GAYLE_CS Register (0xDA8000)

```
Bit 7 - GAYLE_CS_IDE     IDE interrupt status
Bit 6 - GAYLE_CS_CCDET   Credit card detected
Bit 5 - GAYLE_CS_BVD1    Battery voltage detect 1
Bit 2 - GAYLE_CS_BSY     Card busy
Bit 1 - GAYLE_CS_DAEN    Digital audio enable
Bit 0 - GAYLE_CS_DIS     Disable PCMCIA slot
```

### GAYLE_IRQ Register (0xDA9000)

```
Bit 7 - GAYLE_IRQ_IDE    IDE interrupt
Bit 6 - GAYLE_IRQ_CCDET  Card detect interrupt
Bit 5 - GAYLE_IRQ_BVD1   Battery detect 1 interrupt
Bit 2 - GAYLE_IRQ_BSY    Card busy interrupt
Bit 1 - GAYLE_IRQ_RESET  Reset after card detect
Bit 0 - GAYLE_IRQ_BERR   Bus error after card detect

Write 1 to clear interrupt (AND operation)
```

### GAYLE_INT Register (0xDAA000)

```
Bit 7 - GAYLE_INT_IDE      Enable IDE interrupts
Bit 6 - GAYLE_INT_CCDET    Enable card detect interrupt
Bit 5 - GAYLE_INT_BVD1     Enable battery detect interrupt
Bit 2 - GAYLE_INT_BSY      Enable card busy interrupt
Bit 1 - GAYLE_INT_BVD_LEV  BVD interrupt level (0=IRQ2, 1=IRQ6)
Bit 0 - GAYLE_INT_BSY_LEV  BSY interrupt level (0=IRQ2, 1=IRQ6)
```

### GAYLE_CFG Register (0xDAB000)

```
Bits 3-2: Access timing
  0x00 = 250ns
  0x04 = 150ns
  0x08 = 100ns
  0x0C = 720ns

Bits 1-0: Voltage
  0x00 = 0V (off)
  0x01 = 5V
  0x02 = 12V
```

## Common IDE Commands

```
Command  Code  Description
-------  ----  -----------
READ SECTORS        0x20  Read sectors with retry
READ SECTORS NORET  0x21  Read sectors without retry
WRITE SECTORS       0x30  Write sectors with retry
WRITE SECTORS NORET 0x31  Write sectors without retry
IDENTIFY DEVICE     0xEC  Get drive information
SET FEATURES        0xEF  Set transfer mode/features
FLUSH CACHE         0xE7  Flush write cache to disk
RECALIBRATE         0x10  Recalibrate (seek to cylinder 0)
```

## Assembly Code Examples

### Constants Definition

```asm
; IDE Controller Base
GAYLE_IDE_BASE   = $DA0000

; IDE Registers
IDE_DATA         = $DA0000
IDE_ERROR        = $DA0100
IDE_FEATURE      = $DA0100
IDE_NSECTOR      = $DA0200
IDE_SECTOR       = $DA0300
IDE_LCYL         = $DA0400
IDE_HCYL         = $DA0500
IDE_SELECT       = $DA0600
IDE_STATUS       = $DA0700
IDE_COMMAND      = $DA0700

; GAYLE Chip Registers
GAYLE_CS         = $DA8000
GAYLE_IRQ        = $DA9000
GAYLE_INT        = $DAA000
GAYLE_CFG        = $DAB000

; Status Bits
STATUS_BSY       = 7
STATUS_DRDY      = 6
STATUS_DRQ       = 3
STATUS_ERR       = 0

; Error Bits
ERR_BBK          = $80
ERR_UNC          = $40
ERR_IDNF         = $10
ERR_ABRT         = $04

; Commands
CMD_READ_SECTORS = $20
CMD_WRITE_SECTORS = $30
CMD_IDENTIFY     = $EC
CMD_FLUSH_CACHE  = $E7
```

### Initialization and Detection

```asm
;===========================================================================
; Initialize IDE controller and detect drive
;===========================================================================
init_ide:
    ; Check if IDE drive exists
    ; QUIRK: GAYLE returns 0x7F if no drive (not 0xFF like PC IDE)
    move.b  IDE_STATUS,d0
    cmpi.b  #$7F,d0
    beq.s   .no_drive

    ; Wait for drive ready after power-on
.wait_init:
    move.b  IDE_STATUS,d0
    btst    #STATUS_BSY,d0
    bne.s   .wait_init

    ; Select master drive, no LBA yet
    move.b  #$A0,IDE_SELECT

    ; Small delay for drive selection
    moveq   #10,d0
.delay:
    dbf     d0,.delay

    ; Optionally identify the drive
    bsr     identify_drive

    moveq   #0,d0               ; Success
    rts

.no_drive:
    moveq   #-1,d0              ; No drive present
    rts
```

### Drive Identification

```asm
;===========================================================================
; Identify IDE drive and get parameters
; Returns: Drive info in id_buffer (512 bytes)
;===========================================================================
identify_drive:
    ; Wait for ready
.wait_rdy:
    move.b  IDE_STATUS,d0
    btst    #STATUS_BSY,d0
    bne.s   .wait_rdy

    ; Select master drive
    move.b  #$A0,IDE_SELECT

    ; Send IDENTIFY DEVICE command
    move.b  #CMD_IDENTIFY,IDE_COMMAND

    ; Wait for DRQ or error
.wait_id:
    move.b  IDE_STATUS,d0
    btst    #STATUS_ERR,d0
    bne.s   .id_error
    btst    #STATUS_DRQ,d0
    beq.s   .wait_id

    ; Read 256 words (512 bytes) of identify data
    lea     id_buffer,a0
    move.w  #255,d1
.id_loop:
    move.w  IDE_DATA,(a0)+
    dbf     d1,.id_loop

    ; Parse important fields from identify data:
    ; Word 60-61: Total addressable sectors (28-bit LBA)
    ; Word 100-103: Total addressable sectors (48-bit LBA)
    ; Word 1: Number of cylinders
    ; Word 3: Number of heads
    ; Word 6: Sectors per track

    moveq   #0,d0               ; Success
    rts

.id_error:
    moveq   #-1,d0              ; Identification failed
    rts

id_buffer:
    ds.b    512                 ; Buffer for identify data
```

### Read Single Sector

```asm
;===========================================================================
; Read one sector from IDE drive
; Input: d0.l = LBA sector number
;        a0   = buffer address (must be 512 bytes)
; Output: d0.l = 0 if success, error code if failure
;===========================================================================
read_sector:
    move.l  d0,d2               ; Save LBA

    ; Wait for drive ready
.wait_rdy:
    move.b  IDE_STATUS,d0
    btst    #STATUS_BSY,d0
    bne.s   .wait_rdy

    ; Set up LBA read (28-bit addressing)
    move.b  #1,IDE_NSECTOR      ; Read 1 sector

    ; Program LBA address
    move.l  d2,d0
    move.b  d0,IDE_SECTOR       ; LBA bits 0-7
    lsr.l   #8,d0
    move.b  d0,IDE_LCYL         ; LBA bits 8-15
    lsr.l   #8,d0
    move.b  d0,IDE_HCYL         ; LBA bits 16-23
    lsr.l   #8,d0
    andi.b  #$0F,d0             ; LBA bits 24-27
    ori.b   #$E0,d0             ; Set LBA mode + master drive
    move.b  d0,IDE_SELECT

    ; Send READ SECTORS command
    move.b  #CMD_READ_SECTORS,IDE_COMMAND

    ; Wait for DRQ (data ready) or error
.wait_data:
    move.b  IDE_STATUS,d0
    btst    #STATUS_ERR,d0
    bne.s   .read_error
    btst    #STATUS_DRQ,d0
    beq.s   .wait_data

    ; Read 256 words (512 bytes)
    move.w  #255,d1
.read_loop:
    move.w  IDE_DATA,(a0)+
    dbf     d1,.read_loop

    ; Wait for completion
.wait_done:
    move.b  IDE_STATUS,d0
    btst    #STATUS_BSY,d0
    bne.s   .wait_done

    ; Check for errors
    btst    #STATUS_ERR,d0
    bne.s   .read_error

    moveq   #0,d0               ; Success
    rts

.read_error:
    move.b  IDE_ERROR,d0        ; Return error code
    ext.w   d0
    ext.l   d0
    rts
```

### Write Single Sector

```asm
;===========================================================================
; Write one sector to IDE drive
; Input: d0.l = LBA sector number
;        a0   = buffer address (must be 512 bytes)
; Output: d0.l = 0 if success, error code if failure
;===========================================================================
write_sector:
    move.l  d0,d2               ; Save LBA

    ; Wait for drive ready
.wait_rdy:
    move.b  IDE_STATUS,d0
    btst    #STATUS_BSY,d0
    bne.s   .wait_rdy

    ; Set up LBA write
    move.b  #1,IDE_NSECTOR

    ; Program LBA address
    move.l  d2,d0
    move.b  d0,IDE_SECTOR
    lsr.l   #8,d0
    move.b  d0,IDE_LCYL
    lsr.l   #8,d0
    move.b  d0,IDE_HCYL
    lsr.l   #8,d0
    andi.b  #$0F,d0
    ori.b   #$E0,d0
    move.b  d0,IDE_SELECT

    ; Send WRITE SECTORS command
    move.b  #CMD_WRITE_SECTORS,IDE_COMMAND

    ; Wait for DRQ (ready to receive data)
.wait_wdata:
    move.b  IDE_STATUS,d0
    btst    #STATUS_ERR,d0
    bne.s   .write_error
    btst    #STATUS_DRQ,d0
    beq.s   .wait_wdata

    ; Write 256 words (512 bytes)
    move.w  #255,d1
.write_loop:
    move.w  (a0)+,IDE_DATA
    dbf     d1,.write_loop

    ; Wait for completion
.wait_done:
    move.b  IDE_STATUS,d0
    btst    #STATUS_BSY,d0
    bne.s   .wait_done

    ; Check for errors
    btst    #STATUS_ERR,d0
    bne.s   .write_error

    moveq   #0,d0               ; Success
    rts

.write_error:
    move.b  IDE_ERROR,d0        ; Return error code
    ext.w   d0
    ext.l   d0
    rts
```

### Multi-Sector Read

```asm
;===========================================================================
; Read multiple sectors from IDE drive
; Input: d0.l = starting LBA sector number
;        d1.w = sector count (1-256, where 0 means 256)
;        a0   = buffer address
; Output: d0.l = 0 if success, error code if failure
;===========================================================================
read_multi:
    move.l  d0,d2               ; Save starting LBA
    move.w  d1,d3               ; Save sector count

    ; Wait for ready
.wait_rdy:
    move.b  IDE_STATUS,d0
    btst    #STATUS_BSY,d0
    bne.s   .wait_rdy

    ; Set sector count (0 = 256 sectors)
    move.b  d3,IDE_NSECTOR

    ; Program starting LBA
    move.l  d2,d0
    move.b  d0,IDE_SECTOR
    lsr.l   #8,d0
    move.b  d0,IDE_LCYL
    lsr.l   #8,d0
    move.b  d0,IDE_HCYL
    lsr.l   #8,d0
    andi.b  #$0F,d0
    ori.b   #$E0,d0
    move.b  d0,IDE_SELECT

    ; Send READ SECTORS command
    move.b  #CMD_READ_SECTORS,IDE_COMMAND

    ; Adjust count (0 = 256)
    tst.w   d3
    bne.s   .count_ok
    move.w  #256,d3
.count_ok:

    ; Read each sector
.sector_loop:
    ; Wait for DRQ
.wait_drq:
    move.b  IDE_STATUS,d0
    btst    #STATUS_ERR,d0
    bne.s   .multi_error
    btst    #STATUS_DRQ,d0
    beq.s   .wait_drq

    ; Read 256 words (one sector)
    move.w  #255,d1
.word_loop:
    move.w  IDE_DATA,(a0)+
    dbf     d1,.word_loop

    ; Next sector
    subq.w  #1,d3
    bne.s   .sector_loop

    ; Wait for final completion
.wait_done:
    move.b  IDE_STATUS,d0
    btst    #STATUS_BSY,d0
    bne.s   .wait_done

    ; Check final status
    btst    #STATUS_ERR,d0
    bne.s   .multi_error

    moveq   #0,d0               ; Success
    rts

.multi_error:
    move.b  IDE_ERROR,d0
    ext.w   d0
    ext.l   d0
    rts
```

### Flush Cache

```asm
;===========================================================================
; Flush IDE write cache to ensure data is on disk
; Output: d0.l = 0 if success, error code if failure
;===========================================================================
flush_cache:
    ; Wait for ready
.wait_rdy:
    move.b  IDE_STATUS,d0
    btst    #STATUS_BSY,d0
    bne.s   .wait_rdy

    ; Send FLUSH CACHE command
    move.b  #CMD_FLUSH_CACHE,IDE_COMMAND

    ; Wait for completion (may take a while)
.wait_done:
    move.b  IDE_STATUS,d0
    btst    #STATUS_BSY,d0
    bne.s   .wait_done

    ; Check for errors
    btst    #STATUS_ERR,d0
    bne.s   .error

    moveq   #0,d0               ; Success
    rts

.error:
    move.b  IDE_ERROR,d0
    ext.w   d0
    ext.l   d0
    rts
```

## Interrupt Handling

### Enable IDE Interrupts

```asm
;===========================================================================
; Enable IDE interrupts (Level 2)
;===========================================================================
enable_ide_interrupts:
    ; Enable IDE interrupt in GAYLE
    move.b  #$80,GAYLE_INT      ; Bit 7 = IDE interrupt enable

    ; Set up IRQ2 vector
    move.l  #ide_irq_handler,$68

    rts
```

### IRQ2 Handler

```asm
;===========================================================================
; IDE interrupt handler (IRQ2)
;===========================================================================
ide_irq_handler:
    movem.l d0-d1/a0-a1,-(sp)

    ; Check if this is an IDE interrupt
    move.b  GAYLE_IRQ,d0
    btst    #7,d0               ; Test IDE IRQ bit
    beq.s   .not_ide

    ; Handle IDE interrupt
    ; (Your interrupt handling code here)
    ; - Check what operation completed
    ; - Update flags/buffers
    ; - Wake up waiting processes

    ; Clear IDE interrupt by writing 1 to bit 7
    move.b  #$80,GAYLE_IRQ

.not_ide:
    movem.l (sp)+,d0-d1/a0-a1
    rte
```

## Important Quirks and Notes

### 1. No Drive Detection

**GAYLE returns `0x7F` when no drive is present**, not `0xFF` like standard PC IDE controllers. This is an intentional "IDE killer" feature to speed up boot times when no IDE drive is installed.

```asm
move.b  IDE_STATUS,d0
cmpi.b  #$7F,d0
beq     no_drive_present
```

### 2. Register Spacing

Registers are spaced **0x100 bytes (256 bytes) apart**, not 1 byte like PC IDE. This is critical for correct addressing.

```
Correct:   IDE_DATA = 0xDA0000, IDE_ERROR = 0xDA0100
Incorrect: IDE_DATA = 0xDA0000, IDE_ERROR = 0xDA0001
```

### 3. Byte Order (Endianness)

Data is read/written in Motorola (big-endian) format, unlike PC systems which use little-endian. This affects multi-byte values in the IDENTIFY DEVICE data.

### 4. Secondary Drive

Accessing a secondary/slave drive requires:
1. An IDE splitter cable
2. Adding 0x400 to all register addresses
3. Setting bit 4 in IDE_SELECT register

Without a splitter, only the master drive is accessible.

### 5. Interrupt Level

IDE interrupts are **Level 2 (IRQ2)**, not edge-triggered. The interrupt must be explicitly cleared by writing to GAYLE_IRQ.

### 6. Timing

No special timing delays are required on real hardware or in fs-uae. The emulator and hardware both handle proper IDE timing automatically.

### 7. Data Port Width

The IDE_DATA register is 16-bit. Reading/writing must be done as word (16-bit) operations for proper data transfer.

### 8. Status Register Polling

Always check BSY (busy) bit before issuing new commands. Check DRQ (data request) bit before reading/writing data. Check ERR (error) bit after operations complete.

## A600 vs A1200 Differences

**There are no significant differences** in the IDE interface between A600 and A1200:
- Both use the same base address (0xDA0000)
- Both use the GAYLE chip with identical functionality
- Both use the same register layout and protocol
- Both support the same IDE/ATA command set

Code written for one will work on the other without modification.

## Drive Information from IDENTIFY DEVICE

After executing the IDENTIFY DEVICE command (0xEC), 512 bytes of drive information are returned. Key fields (all values in word format, big-endian):

```
Word Offset  Field
-----------  -----
1            Number of cylinders (obsolete for LBA)
3            Number of heads (obsolete for LBA)
6            Sectors per track (obsolete for LBA)
10-19        Serial number (20 ASCII characters, swapped)
23-26        Firmware revision (8 ASCII characters, swapped)
27-46        Model number (40 ASCII characters, swapped)
47           Max sectors per multi-sector transfer
49           Capabilities (bit 9 = LBA supported)
60-61        Total sectors addressable in 28-bit LBA (32-bit value)
100-103      Total sectors addressable in 48-bit LBA (64-bit value)
```

Note: String values have bytes swapped within each word (e.g., "AB" stored as "BA").

## References

### FS-UAE Source Files

- `/ide.cpp` - Main IDE emulation (2838 lines)
- `/idecontrollers.cpp` - Controller variants (1180 lines)
- `/gayle.cpp` - GAYLE chip emulation
- `/include/ide.h` - IDE register definitions (148 lines)
- `/include/gayle.h` - GAYLE chip definitions

### ATA/ATAPI Specifications

For detailed information on IDE/ATA commands and protocols, refer to:
- ATA/ATAPI-6 specification
- ATA/ATAPI-7 specification

## Revision History

- 2026-02-01: Initial version based on fs-uae codebase analysis

---

**Document prepared for bare-metal Amiga ROM/OS development**
