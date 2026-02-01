# FS-UAE RDB and Hardfile Implementation - Reference Guide

## Overview

This document explains how FS-UAE handles Rigid Disk Blocks (RDB) and partition structures when using hardfile (HDF) images with IDE emulation. This information is critical for developers writing bare-metal ROM code or custom operating systems that need to read partition tables and boot from IDE drives.

**Key Point:** The RDB in FS-UAE is **VIRTUAL** - it exists only in RAM, not in the actual HDF file.

## Table of Contents

1. [Virtual vs Physical RDB](#virtual-vs-physical-rdb)
2. [Memory Layout](#memory-layout)
3. [RDB Structure](#rdb-structure)
4. [Partition Block Structure](#partition-block-structure)
5. [Block Read/Write Handling](#block-readwrite-handling)
6. [What Your ROM Sees](#what-your-rom-sees)
7. [Implementation Examples](#implementation-examples)
8. [Hardfile Types](#hardfile-types)

---

## Virtual vs Physical RDB

### Virtual RDB (Default)

**The RDB is created in RAM by FS-UAE**, not stored in the HDF file itself.

**From `hardfile.cpp:402-566`:**
```c
uae_u8 *rdb = xcalloc(uae_u8, size);  // Allocate RAM buffer
hfd->virtual_rdb = rdb;               // Store pointer
hfd->virtual_size = size;             // Typically ~262KB (512 blocks)
```

**What's in the virtual RDB:**
- Block 0: RDB Header ("RDSK" magic)
- Block 1: PART Block (partition table entry)
- Block 2: FSHD Block (filesystem header)
- Block 3+: LSEG Blocks (filesystem code segments)

### Physical RDB

**FS-UAE uses the physical RDB when:**
- The HDF file's block 0 starts with "RDSK" magic (0x5244534B)
- The HDF was created with HDToolBox or similar partitioning tools

**From `hardfile.cpp:600-603`:**
```c
if (buf[0] != 0 && memcmp(buf, "RDSK", 4)) {
    // No RDSK magic found - create virtual RDB
    create_virtual_rdb(&hfd->hfd);
}
```

### When is Virtual RDB Created?

Virtual RDB is created when:
1. HDF file exists but block 0 is NOT "RDSK"
2. HDF is a plain filesystem image without partition table
3. Configuration specifies geometry but HDF has no RDB

---

## Memory Layout

```
┌─────────────────────────────────────┐
│   RAM (virtual_rdb buffer)          │
│   Size: ~262KB (512 blocks)         │
│                                     │
│   Block 0: RDB Header (512 bytes)  │ ← LBA 0 reads from here
│   Block 1: PART block (512 bytes)  │ ← LBA 1 reads from here
│   Block 2: FSHD block (512 bytes)  │ ← LBA 2 reads from here
│   Block 3+: LSEG blocks            │ ← LBA 3+ reads from here
│   ...                               │
│   (End of virtual RDB)              │
└─────────────────────────────────────┘
              ↓
    virtual_size boundary (offset 262,144)
              ↓
┌─────────────────────────────────────┐
│   HDF File (physical disk image)    │
│                                     │
│   Actual filesystem data            │ ← LBA 512+ reads from here
│   Files, directories, boot blocks   │
│   (HDF file offset 0 = LBA 512)     │
│                                     │
└─────────────────────────────────────┘
```

**Critical Mapping:**
- **LBA 0-511**: Read from `virtual_rdb` buffer in RAM
- **LBA 512+**: Read from HDF file (offset = (LBA * 512) - virtual_size)

---

## RDB Structure

### RDB Header Block (Block 0)

When you read LBA 0 via IDE, you receive this structure:

```
Offset  Size  Field              Description
------  ----  -----              -----------
0       4     rdb_ID             "RDSK" magic (0x5244534B)
4       4     rdb_SummedLongs    Size of checksummed structure
8       4     rdb_ChkSum         Block checksum
12      4     rdb_HostID         SCSI Host ID (usually 7)
16      4     rdb_BlockBytes     Bytes per block (512)
20      4     rdb_Flags          Flags (0x17 typical)
24      4     rdb_BadBlockList   Block number of bad block list (-1 = none)
28      4     rdb_PartitionList  Block number of first PART block (usually 1)
32      4     rdb_FileSysHdrList Block number of first FSHD block (usually 2)
36      4     rdb_DriveInit      Drive init code
40      8     rdb_Reserved1      Reserved
48      4     rdb_Cylinders      Number of cylinders
52      4     rdb_Sectors        Sectors per track
56      4     rdb_Heads          Number of heads
60      4     rdb_Interleave     Interleave
64      4     rdb_Park           Parking cylinder
68      4     rdb_Reserved2
72      4     rdb_WritePreComp   Write precompensation cylinder
76      4     rdb_ReducedWrite   Reduced write current cylinder
80      4     rdb_StepRate       Step rate
84     20     rdb_Reserved3
104     4     rdb_RDBBlocksLo    Low block of RDB area
108     4     rdb_RDBBlocksHi    High block of RDB area
112     4     rdb_LoCylinder     Low cylinder of partitionable area
116     4     rdb_HiCylinder     High cylinder of partitionable area
120     4     rdb_CylBlocks      Blocks per cylinder
124     4     rdb_AutoParkSeconds Auto-park timeout
128     4     rdb_HighRDSKBlock  Highest block used by RDB
132    20     rdb_Reserved4
152     8     rdb_DiskVendor     Vendor name
160     8     rdb_DiskProduct    Product name
168     4     rdb_DiskRevision   Product revision
176     8     rdb_ControllerVendor Controller vendor
184    12     rdb_ControllerProduct Controller product
196     4     rdb_ControllerRevision Controller revision
200    56     rdb_Reserved5
```

**Key Fields for ROM Developers:**
- **Offset 0**: Must be "RDSK" to identify valid RDB
- **Offset 28**: Points to first partition block
- **Offset 48**: Cylinders (geometry)
- **Offset 52**: Sectors per track (geometry)
- **Offset 56**: Heads (geometry)

---

## Partition Block Structure

### PART Block (Block 1, typically)

```
Offset  Size  Field              Description
------  ----  -----              -----------
0       4     pb_ID              "PART" magic (0x50415254)
4       4     pb_SummedLongs     Size of checksummed structure
8       4     pb_ChkSum          Block checksum
12      4     pb_HostID          SCSI Target ID
16      4     pb_Next            Next PART block (-1 = none)
20      4     pb_Flags           Partition flags (bit 0 = bootable)
24      2     pb_Reserved1
26      2     pb_DevFlags        Device flags
28     32     pb_DriveName       Partition name (BCPL string, e.g., "DH0")
60     16     pb_Reserved2
76      4     pb_SizeBlock       Size of block in longwords (128 for 512 bytes)
80      4     pb_SecOrg          Reserved (0)
84      4     pb_Surfaces        Number of heads
88      4     pb_SectorPerBlock  Sectors per block (1)
92      4     pb_BlocksPerTrack  Blocks per track
96      4     pb_Reserved        Reserved blocks at start
100     4     pb_PreAlloc        Preallocated blocks
104     4     pb_Interleave      Interleave
108     4     pb_LowCyl          Low cylinder of partition
112     4     pb_HighCyl         High cylinder of partition
116     4     pb_NumBuffer       Number of buffers
120     4     pb_BufMemType      Buffer memory type
124     4     pb_MaxTransfer     Maximum transfer size
128     4     pb_Mask            Address mask
132     4     pb_BootPri         Boot priority (-128 to 127)
136     4     pb_DosType         Filesystem type (0x444f5301 = "DOS\1")
140     4     pb_Baud            Baud rate
144     4     pb_Control         Control word
148     4     pb_BootBlocks      Number of boot blocks
152    104    pb_Reserved3
```

**Key Fields for ROM Developers:**
- **Offset 0**: Must be "PART" to identify valid partition
- **Offset 16**: Next partition block number (-1 if last)
- **Offset 20**: Bit 0 = bootable flag
- **Offset 28**: Partition name (BCPL string)
- **Offset 108**: Low cylinder (start of partition)
- **Offset 112**: High cylinder (end of partition)
- **Offset 132**: Boot priority (higher = boot first)
- **Offset 136**: DOS type (filesystem identifier)

---

## Block Read/Write Handling

### Read Operation

**From `hardfile.cpp:1122-1137`:**

```c
static int hdf_read2(struct hardfiledata *hfd, void *buffer, uae_u64 offset, int len)
{
    if (offset < hfd->virtual_size) {
        // Reading from virtual RDB area
        uae_s64 len2 = offset + len <= hfd->virtual_size ?
                       len : hfd->virtual_size - offset;
        if (!hfd->virtual_rdb)
            return 0;
        memcpy(buffer, hfd->virtual_rdb + offset, (size_t)len2);
        return len2;
    } else {
        // Reading from actual HDF file
        offset -= hfd->virtual_size;
        fseek(hfd->handle, offset, SEEK_SET);
        return fread(buffer, 1, len, hfd->handle);
    }
}
```

**Read Flow:**
```
IDE Read LBA N:
  ↓
Calculate offset = N × 512
  ↓
Is offset < virtual_size (262,144)?
  ↓ YES                        ↓ NO
Return from RAM             Return from file
virtual_rdb[offset]         at (offset - virtual_size)
```

### Write Operation

**From `hardfile.cpp:1182+`:**

```c
static int hdf_write2(struct hardfiledata *hfd, void *buffer, uae_u64 offset, int len)
{
    if (offset < hfd->virtual_size) {
        // Writing to virtual RDB area - IGNORED!
        // Virtual RDB is read-only
        return len;  // Pretend success but don't write
    } else {
        // Writing to actual HDF file
        offset -= hfd->virtual_size;
        fseek(hfd->handle, offset, SEEK_SET);
        return fwrite(buffer, 1, len, hfd->handle);
    }
}
```

**Critical:** Writes to the virtual RDB area (blocks 0-511) are **silently ignored**. The virtual RDB is read-only.

---

## What Your ROM Sees

### Reading Block 0 (RDB Header)

```asm
; Your ROM code
    moveq   #0,d0           ; LBA 0
    lea     buffer,a0
    bsr     read_sector     ; Read via IDE at 0xDA0002-0xDA001E

    ; What you get in buffer:
    ; Offset 0:  'R' 'D' 'S' 'K'  (0x52 0x44 0x53 0x4B)
    ; Offset 28: 0x00000001        (partition at block 1)
    ; Offset 32: 0x00000002        (filesystem at block 2)
    ; Offset 48: Cylinders
    ; Offset 52: Sectors per track
    ; Offset 56: Heads
```

### Reading Block 1 (Partition Block)

```asm
; Your ROM code
    moveq   #1,d0           ; LBA 1
    lea     buffer,a0
    bsr     read_sector

    ; What you get in buffer:
    ; Offset 0:  'P' 'A' 'R' 'T'  (0x50 0x41 0x52 0x54)
    ; Offset 16: 0xFFFFFFFF        (next partition, -1 = none)
    ; Offset 20: 0x00000001        (flags, bit 0 = bootable)
    ; Offset 28: "\003DH0"         (BCPL string "DH0")
    ; Offset 108: Low cylinder
    ; Offset 112: High cylinder
    ; Offset 132: Boot priority
    ; Offset 136: DOS type (0x444F5301 = "DOS\1")
```

### Reading Block 512+ (Filesystem Data)

```asm
; Your ROM code
    move.l  #512,d0         ; LBA 512
    lea     buffer,a0
    bsr     read_sector

    ; What you get in buffer:
    ; Data from HDF file offset 0
    ; (offset = 512*512 - 262144 = 0)
    ; This is the actual filesystem boot block or data
```

---

## Implementation Examples

### Example 1: Detect and Parse RDB

```asm
;===========================================================================
; Detect RDB and get disk geometry
; Output: d0.l = cylinders
;         d1.l = heads
;         d2.l = sectors per track
;         d3.l = partition block number (or -1)
;===========================================================================
detect_rdb:
    ; Read block 0
    moveq   #0,d0
    lea     rdb_buffer,a0
    bsr     read_sector
    tst.l   d0
    bne.s   .error

    ; Check for RDB magic
    lea     rdb_buffer,a0
    cmpi.l  #'RDSK',(a0)
    bne.s   .no_rdb

    ; Verify checksum (optional but recommended)
    bsr     verify_rdb_checksum
    tst.l   d0
    bne.s   .error

    ; Extract geometry
    lea     rdb_buffer,a0
    move.l  48(a0),d0       ; Cylinders
    move.l  56(a0),d1       ; Heads
    move.l  52(a0),d2       ; Sectors per track

    ; Get partition list pointer
    move.l  28(a0),d3       ; First PART block

    moveq   #0,d0           ; Success
    rts

.no_rdb:
    moveq   #-1,d0          ; No RDB found
    rts

.error:
    moveq   #-2,d0          ; Read error
    rts

rdb_buffer:
    ds.b    512
```

### Example 2: Find Bootable Partition

```asm
;===========================================================================
; Find the highest priority bootable partition
; Input:  d3.l = first partition block number
; Output: d0.l = partition start LBA (or -1 if none)
;         d1.l = partition end LBA
;         d2.l = DOS type
;===========================================================================
find_boot_partition:
    move.l  d3,d4           ; Current partition block
    moveq   #-1,d5          ; Best boot priority (start at -1)
    moveq   #-1,d6          ; Best partition block number

.partition_loop:
    ; Check if end of partition list
    cmpi.l  #-1,d4
    beq.s   .done_searching

    ; Read partition block
    move.l  d4,d0
    lea     part_buffer,a0
    bsr     read_sector
    tst.l   d0
    bne.s   .error

    ; Verify PART magic
    lea     part_buffer,a0
    cmpi.l  #'PART',(a0)
    bne.s   .error

    ; Check if bootable (bit 0 of flags)
    move.l  20(a0),d0
    btst    #0,d0
    beq.s   .not_bootable

    ; Get boot priority
    move.l  132(a0),d0      ; Boot priority (signed)
    ext.l   d0              ; Sign extend

    ; Compare with best so far
    cmp.l   d5,d0
    ble.s   .not_bootable   ; Lower or equal priority

    ; This is the new best
    move.l  d0,d5           ; New best priority
    move.l  d4,d6           ; Remember this block

.not_bootable:
    ; Get next partition
    lea     part_buffer,a0
    move.l  16(a0),d4       ; Next PART block
    bra.s   .partition_loop

.done_searching:
    ; Check if we found any bootable partition
    cmpi.l  #-1,d6
    beq.s   .no_bootable

    ; Re-read the best partition
    move.l  d6,d0
    lea     part_buffer,a0
    bsr     read_sector

    ; Calculate partition boundaries
    lea     part_buffer,a0
    move.l  108(a0),d0      ; Low cylinder
    move.l  112(a0),d1      ; High cylinder
    move.l  92(a0),d2       ; Blocks per track
    move.l  84(a0),d3       ; Surfaces (heads)

    ; Convert to LBA
    ; Start LBA = low_cyl * heads * sectors_per_track
    mulu.w  d3,d0           ; low_cyl * heads
    mulu.w  d2,d0           ; * sectors_per_track

    ; End LBA = (high_cyl + 1) * heads * sectors_per_track - 1
    addq.l  #1,d1           ; high_cyl + 1
    mulu.w  d3,d1
    mulu.w  d2,d1
    subq.l  #1,d1

    ; Get DOS type
    lea     part_buffer,a0
    move.l  136(a0),d2      ; DOS type

    moveq   #0,d7           ; Success
    rts

.no_bootable:
    moveq   #-1,d0          ; No bootable partition
    rts

.error:
    moveq   #-2,d0          ; Error
    rts

part_buffer:
    ds.b    512
```

### Example 3: Calculate Block Number to LBA

```asm
;===========================================================================
; Convert partition block number to absolute LBA
; Input:  d0.l = block number within partition
;         d1.l = partition start LBA
;         d2.l = partition end LBA
; Output: d0.l = absolute LBA (or -1 if out of range)
;===========================================================================
partition_block_to_lba:
    ; Check range
    move.l  d0,d3
    add.l   d1,d3           ; Absolute LBA
    cmp.l   d2,d3           ; Compare with end
    bgt.s   .out_of_range

    move.l  d3,d0           ; Return absolute LBA
    rts

.out_of_range:
    moveq   #-1,d0
    rts
```

### Example 4: Verify RDB Checksum

```asm
;===========================================================================
; Verify RDB checksum
; Input:  a0 = pointer to RDB block
; Output: d0.l = 0 if valid, -1 if invalid
;===========================================================================
verify_rdb_checksum:
    movem.l d1-d3/a0,-(sp)

    ; Get summed longs count
    move.l  4(a0),d1        ; rdb_SummedLongs

    ; Calculate checksum
    moveq   #0,d2           ; Checksum accumulator
    moveq   #0,d3           ; Counter

.checksum_loop:
    cmp.l   d1,d3
    bge.s   .checksum_done

    ; Skip the checksum field itself (offset 8)
    move.l  d3,d0
    lsl.l   #2,d0           ; Convert to byte offset
    cmpi.l  #8,d0
    beq.s   .skip_field

    add.l   (a0,d0.l),d2    ; Add long to checksum

.skip_field:
    addq.l  #1,d3
    bra.s   .checksum_loop

.checksum_done:
    ; Negate checksum
    neg.l   d2

    ; Compare with stored checksum
    move.l  8(a0),d0
    cmp.l   d0,d2
    bne.s   .invalid

    moveq   #0,d0           ; Valid
    movem.l (sp)+,d1-d3/a0
    rts

.invalid:
    moveq   #-1,d0          ; Invalid
    movem.l (sp)+,d1-d3/a0
    rts
```

---

## Hardfile Types

### FILESYS_HARDFILE (Type 1)

**No RDB mode** - Direct filesystem access

```
Block 0: Filesystem boot block or root directory
         Starts with "DOS\0" through "DOS\7" magic
         OR filesystem-specific signature

Block 1+: Filesystem data (directories, files, etc.)
```

**Characteristics:**
- No partition table
- No bootable flag
- Direct mounting
- Uses configured block size (512, 1024, 2048, etc.)
- Simpler structure

**Use case:** Single partition, no multi-boot, direct filesystem image

### FILESYS_HARDFILE_RDB (Type 2)

**With RDB mode** - Full partition table support

```
Block 0: RDB header ("RDSK")
Block 1: PART block (partition 1)
Block 2: FSHD block (filesystem header)
Block 3+: LSEG blocks (filesystem code)
...
Block 512+: Actual partition data
```

**Characteristics:**
- Full RDB/partition structure
- Multiple partitions supported
- Boot priorities
- Geometry override
- Always uses 512-byte blocks for IDE

**Use case:** Multi-partition, bootable, complex disk layouts

---

## Practical Examples

### Example HDF Layout (100MB disk)

```
Virtual RDB (in RAM):
┌────────────────────────┐
│ Block 0: RDSK          │  Offset 0
│   Cylinders: 812       │
│   Heads: 16            │
│   Sectors: 16          │
│   PartitionList: 1     │
│   FileSysHdrList: 2    │
├────────────────────────┤
│ Block 1: PART          │  Offset 512
│   Name: "DH0"          │
│   LowCyl: 2            │
│   HighCyl: 811         │
│   BootPri: 0           │
│   DOSType: DOS\1       │
├────────────────────────┤
│ Block 2: FSHD          │  Offset 1024
│   DOSType: 0x444F5301  │
│   Version: 40.1        │
├────────────────────────┤
│ Blocks 3-511: Unused   │  Offset 1536-262143
└────────────────────────┘

Physical HDF File:
┌────────────────────────┐
│ Boot blocks (2 blocks) │  LBA 512-513 (file offset 0-1023)
├────────────────────────┤
│ Root block             │  LBA 514+ (file offset 1024+)
├────────────────────────┤
│ Bitmap blocks          │
├────────────────────────┤
│ File data              │
│ ...                    │
└────────────────────────┘
```

### LBA to File Offset Calculation

```c
// FS-UAE internal calculation:

uae_u64 offset = lba * 512;

if (offset < virtual_size) {
    // Read from virtual RDB
    source = virtual_rdb + offset;
} else {
    // Read from HDF file
    file_offset = offset - virtual_size;
    source = hdf_file + file_offset;
}
```

**Examples:**
- LBA 0 → offset 0 → virtual_rdb[0]
- LBA 1 → offset 512 → virtual_rdb[512]
- LBA 512 → offset 262144 → hdf_file[0]
- LBA 1024 → offset 524288 → hdf_file[262144]

---

## Key Source Files

| File | Purpose |
|------|---------|
| `hardfile.cpp` | HDF file handling, virtual RDB creation |
| `ide.cpp` | IDE controller emulation, read/write commands |
| `filesys.cpp` | Filesystem handler (for directory mounts) |
| `include/filesys.h` | Structure definitions, constants |

**Key Functions:**
- `create_virtual_rdb()` (hardfile.cpp:402) - Creates virtual RDB in RAM
- `hdf_read2()` (hardfile.cpp:1122) - Read with virtual RDB handling
- `hdf_write2()` (hardfile.cpp:1182) - Write with virtual RDB handling
- `hdf_open()` (hardfile.cpp:583) - Open HDF and detect RDB
- `ide_read_reg()` (ide.cpp) - IDE register read operations
- `ide_write_reg()` (ide.cpp) - IDE register write operations

---

## RDB Field Reference

### Common DOS Types

```
0x444F5300  "DOS\0"  OFS (Old File System)
0x444F5301  "DOS\1"  FFS (Fast File System)
0x444F5302  "DOS\2"  OFS International
0x444F5303  "DOS\3"  FFS International
0x444F5304  "DOS\4"  OFS Directory Cache
0x444F5305  "DOS\5"  FFS Directory Cache
0x444F5306  "DOS\6"  OFS Long Filenames
0x444F5307  "DOS\7"  FFS Long Filenames
0x4D554653  "MUFS"  MultiUser File System
0x50465301  "PFS\1" Professional File System
0x53465300  "SFS\0" Smart File System
```

### Common Flags

**RDB Flags (rdb_Flags at offset 20):**
```
Bit 0: RDBFF_LAST        Last RDB block
Bit 1: RDBFF_LASTLUN     Last logical unit
Bit 2: RDBFF_LASTID      Last ID
Bit 3: RDBFF_NORESELECT  No reselection
Bit 4: RDBFF_DISKID      Disk ID valid
```

**Partition Flags (pb_Flags at offset 20):**
```
Bit 0: PBFF_BOOTABLE     Partition is bootable
Bit 1: PBFF_NOMOUNT      Don't auto-mount
```

---

## Limitations and Notes

### Virtual RDB Limitations

1. **Read-only**: Writes to virtual RDB blocks are ignored
2. **Single partition**: Default virtual RDB creates only one partition
3. **Fixed structure**: Cannot modify partition table at runtime
4. **Size overhead**: First 512 blocks (~262KB) are virtual

### Best Practices for ROM Development

1. **Always check for "RDSK" magic** at block 0
2. **Verify checksums** to ensure RDB integrity
3. **Handle missing RDB gracefully** (some disks may not have one)
4. **Support both RDB and non-RDB formats**
5. **Respect boot priorities** when choosing partition
6. **Parse geometry** from RDB, don't assume standard values
7. **Check partition boundaries** before access
8. **Handle block size correctly** (always 512 for IDE virtual RDB)

### Common Pitfalls

1. **Assuming block 0 has filesystem data** - Check for RDSK first!
2. **Trying to modify virtual RDB** - Writes are ignored
3. **Not accounting for virtual_size offset** - Data starts at LBA 512, not 0
4. **Hardcoding geometry** - Always read from RDB
5. **Ignoring boot priority** - Multiple partitions may exist

---

## Revision History

- 2026-02-01: Initial version based on fs-uae source code analysis
- Document verified against fs-uae hardfile.cpp and ide.cpp implementation

---

**Document prepared for bare-metal Amiga ROM/OS development**
**Verified against FS-UAE source code version analyzed on 2026-02-01**
