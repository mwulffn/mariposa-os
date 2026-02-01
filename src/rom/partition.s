; ============================================================
; partition.s - Rigid Disk Block (RDB) detection
; ============================================================
; Scans blocks 0-15 for RDB structure and displays information
; ============================================================

; ============================================================
; Constants
; ============================================================
RDB_BUFFER      equ $20000          ; Read buffer in chip RAM
PART_BUFFER     equ $21000          ; Partition block buffer
RDB_MAGIC       equ $5244534B       ; "RDSK"
PART_MAGIC      equ $50415254       ; "PART"
RDB_MAX_BLOCK   equ 16              ; Scan blocks 0-15

; RDB structure offsets
RDB_ID          equ 0               ; Magic "RDSK"
RDB_SUMMEDLONGS equ 4               ; Size in longs
RDB_CHKSUM      equ 8               ; Checksum
RDB_BLOCKBYTES  equ 16              ; Bytes per block
RDB_FLAGS       equ 20              ; Flags
RDB_PARTLIST    equ 28              ; Partition list pointer
RDB_CYLINDERS   equ 64              ; Number of cylinders
RDB_SECTORS     equ 68              ; Sectors per track
RDB_HEADS       equ 72              ; Number of heads

; PART structure offsets
PART_ID         equ 0               ; Magic "PART"
PART_SUMMEDLONGS equ 4              ; Size in longs
PART_CHKSUM     equ 8               ; Checksum
PART_HOSTID     equ 12              ; Host ID
PART_NEXT       equ 16              ; Next partition block
PART_FLAGS      equ 20              ; Flags
PART_DEVFLAGS   equ 32              ; Device flags
PART_DRIVENAME  equ 36              ; Drive name (BCPL string, 32 bytes)
PART_DOSENVVEC  equ 128             ; DosEnvVec structure

; DosEnvVec offsets (relative to PART_DOSENVVEC)
DE_TABLESIZE    equ 0               ; Size of table
DE_SIZEBLOCK    equ 4               ; Block size in longs
DE_SECORG       equ 8               ; Sector origin (unused)
DE_SURFACES     equ 12              ; Number of heads
DE_SECPERBLK    equ 16              ; Sectors per block
DE_BLKSPERTRACK equ 20              ; Blocks per track
DE_RESERVEDBLKS equ 24              ; Reserved blocks
DE_PREALLOC     equ 28              ; Preallocated blocks
DE_INTERLEAVE   equ 32              ; Interleave
DE_LOWCYL       equ 36              ; Starting cylinder
DE_HIGHCYL      equ 40              ; Ending cylinder
DE_NUMBUFFERS   equ 44              ; Number of buffers
DE_BUFMEMTYPE   equ 48              ; Buffer memory type
DE_MAXTRANSFER  equ 52              ; Max transfer size
DE_MASK         equ 56              ; Address mask
DE_BOOTPRI      equ 60              ; Boot priority
DE_DOSTYPE      equ 64              ; Filesystem type

; ============================================================
; FindRDB - Scan for Rigid Disk Block
; ============================================================
; Input:  None
; Output: D0.l = 0 success (RDB found), -1 error (no RDB)
; Preserves: D2-D7/A2-A6
; ============================================================
FindRDB:
    movem.l d1-d7/a0-a6,-(sp)

    ; Print start message
    pea     .msg_scanning(pc)
    bsr     SerialPrintf
    addq.l  #4,sp

    ; Loop through blocks 0-15
    moveq   #0,d7                   ; D7 = current block number

.block_loop:
    ; Read current block to RDB_BUFFER
    lea     RDB_BUFFER,a0           ; Destination
    move.l  d7,d0                   ; LBA = current block
    moveq   #1,d1                   ; Read 1 sector
    bsr     ide_read
    tst.l   d0
    bne     .read_error             ; Error reading

    ; Check for RDB magic at start of buffer
    move.l  RDB_BUFFER+RDB_ID,d0
    cmp.l   #RDB_MAGIC,d0
    beq     .found_rdb              ; Found it!

    ; Try next block
    addq.l  #1,d7
    cmp.l   #RDB_MAX_BLOCK,d7
    blt.s   .block_loop

    ; Not found in blocks 0-15
    pea     .msg_not_found(pc)
    bsr     SerialPrintf
    addq.l  #4,sp
    moveq   #-1,d0
    bra     .exit

.found_rdb:
    ; Print RDB information
    move.l  d7,-(sp)                ; Block number
    pea     .msg_found(pc)
    bsr     SerialPrintf
    addq.l  #8,sp

    ; Print block size
    move.l  RDB_BUFFER+RDB_BLOCKBYTES,d0
    move.l  d0,-(sp)
    pea     .msg_blocksize(pc)
    bsr     SerialPrintf
    addq.l  #8,sp

    ; Print cylinders
    move.l  RDB_BUFFER+RDB_CYLINDERS,d0
    move.l  d0,-(sp)
    pea     .msg_cylinders(pc)
    bsr     SerialPrintf
    addq.l  #8,sp

    ; Print heads
    move.l  RDB_BUFFER+RDB_HEADS,d0
    move.l  d0,-(sp)
    pea     .msg_heads(pc)
    bsr     SerialPrintf
    addq.l  #8,sp

    ; Print sectors
    move.l  RDB_BUFFER+RDB_SECTORS,d0
    move.l  d0,-(sp)
    pea     .msg_sectors(pc)
    bsr     SerialPrintf
    addq.l  #8,sp

    ; Print partition list pointer
    move.l  RDB_BUFFER+RDB_PARTLIST,d0
    move.l  d0,-(sp)
    pea     .msg_partlist(pc)
    bsr     SerialPrintf
    addq.l  #8,sp

    ; Success
    moveq   #0,d0
    bra.s   .exit

.read_error:
    ; Print error with block number
    move.l  d7,-(sp)
    pea     .msg_read_error(pc)
    bsr     SerialPrintf
    addq.l  #8,sp
    moveq   #-1,d0

.exit:
    movem.l (sp)+,d1-d7/a0-a6
    rts

; Messages
.msg_scanning:
    dc.b    "RDB: Scanning blocks 0-15...",13,10,0
.msg_found:
    dc.b    "RDB: Found at block %d",13,10,0
.msg_blocksize:
    dc.b    "RDB: Block size: %d bytes",13,10,0
.msg_cylinders:
    dc.b    "RDB: Cylinders: %d",13,10,0
.msg_heads:
    dc.b    "RDB: Heads: %d",13,10,0
.msg_sectors:
    dc.b    "RDB: Sectors: %d",13,10,0
.msg_partlist:
    dc.b    "RDB: Partition list at block: %d",13,10,0
.msg_not_found:
    dc.b    "RDB: Not found in blocks 0-15",13,10,0
.msg_read_error:
    dc.b    "RDB: Error reading block %d",13,10,0
    even

; ============================================================
; LoadPartition - Load partition and return filesystem location
; ============================================================
; Input:  RDB must be loaded in RDB_BUFFER (call FindRDB first)
; Output: D0.l = 0 success, -1 error
;         D1.l = Partition start LBA (if success)
;         D2.l = Partition size in blocks (if success)
; Preserves: D3-D7/A2-A6
; ============================================================
LoadPartition:
    movem.l d3-d7/a0-a6,-(sp)

    ; Read partition block 1
    pea     .msg_loading(pc)
    bsr     SerialPrintf
    addq.l  #4,sp

    lea     PART_BUFFER,a0
    moveq   #1,d0                   ; LBA 1
    moveq   #1,d1                   ; 1 sector
    bsr     ide_read
    tst.l   d0
    bne     .read_error

    ; Check for PART magic
    move.l  PART_BUFFER+PART_ID,d0
    cmp.l   #PART_MAGIC,d0
    beq     .found_part

    pea     .msg_no_part_found(pc)
    bsr     SerialPrintf
    addq.l  #4,sp
    moveq   #-1,d0
    bra     .exit

.found_part:
    pea     .msg_found(pc)
    bsr     SerialPrintf
    addq.l  #4,sp

    ; Extract and print partition name (BCPL string)
    lea     PART_BUFFER+PART_DRIVENAME,a0
    moveq   #0,d0
    move.b  (a0)+,d0                ; Get length byte
    cmp.b   #31,d0                  ; Max 31 chars
    ble.s   .name_ok
    moveq   #31,d0
.name_ok:
    ; Copy name to stack for printf (null-terminated)
    lea     -32(sp),sp              ; Reserve 32 bytes on stack
    move.l  sp,a1                   ; Destination
    move.w  d0,d1                   ; Counter
    beq.s   .name_done
    subq.w  #1,d1
.name_loop:
    move.b  (a0)+,(a1)+
    dbf     d1,.name_loop
.name_done:
    clr.b   (a1)                    ; Null terminate

    ; Print name
    move.l  sp,-(sp)                ; Push name pointer
    pea     .msg_name(pc)
    bsr     SerialPrintf
    addq.l  #8,sp
    lea     32(sp),sp               ; Remove name buffer

    ; Get cylinder range and geometry
    move.l  PART_BUFFER+PART_DOSENVVEC+DE_LOWCYL,d5    ; LowCyl
    move.l  PART_BUFFER+PART_DOSENVVEC+DE_HIGHCYL,d6   ; HighCyl
    move.l  RDB_BUFFER+RDB_HEADS,d7                     ; Heads
    move.l  RDB_BUFFER+RDB_SECTORS,d4                   ; Sectors

    ; Print cylinder range
    move.l  d6,-(sp)                ; HighCyl
    move.l  d5,-(sp)                ; LowCyl
    pea     .msg_cylinders(pc)
    bsr     SerialPrintf
    lea     12(sp),sp

    ; Calculate partition start LBA
    ; Start LBA = LowCyl * Heads * Sectors
    move.l  d5,d0                   ; LowCyl
    mulu.w  d7,d0                   ; * Heads
    mulu.w  d4,d0                   ; * Sectors
    move.l  d0,d2                   ; Save start LBA in D2

    move.l  d0,-(sp)                ; Start LBA
    pea     .msg_start_lba(pc)
    bsr     SerialPrintf
    addq.l  #8,sp

    ; Calculate partition size in blocks
    ; Size = (HighCyl - LowCyl + 1) * Heads * Sectors
    move.l  d6,d0                   ; HighCyl
    sub.l   d5,d0                   ; - LowCyl
    addq.l  #1,d0                   ; + 1 = number of cylinders
    mulu.w  d7,d0                   ; * Heads
    mulu.w  d4,d0                   ; * Sectors
    move.l  d0,d3                   ; Save size in D3

    move.l  d0,-(sp)                ; Size in blocks
    pea     .msg_size(pc)
    bsr     SerialPrintf
    addq.l  #8,sp

    ; Print filesystem location summary
    move.l  d2,-(sp)                ; Start LBA
    pea     .msg_fs_start(pc)
    bsr     SerialPrintf
    addq.l  #8,sp

    move.l  d3,-(sp)                ; Size
    pea     .msg_fs_size(pc)
    bsr     SerialPrintf
    addq.l  #8,sp

    ; Print DosType
    move.l  PART_BUFFER+PART_DOSENVVEC+DE_DOSTYPE,d0
    move.l  d0,-(sp)
    pea     .msg_dostype(pc)
    bsr     SerialPrintf
    addq.l  #8,sp

    ; Print next partition pointer
    move.l  PART_BUFFER+PART_NEXT,d0
    move.l  d0,-(sp)
    pea     .msg_next(pc)
    bsr     SerialPrintf
    addq.l  #8,sp

    ; Success - return values in D1 and D2
    ; D2 already contains start LBA, D3 contains size
    move.l  d2,d1                   ; D1 = partition start LBA
    move.l  d3,d2                   ; D2 = partition size in blocks
    moveq   #0,d0
    bra.s   .exit

.read_error:
    pea     .msg_read_err(pc)
    bsr     SerialPrintf
    addq.l  #4,sp
    moveq   #-1,d0

.exit:
    movem.l (sp)+,d3-d7/a0-a6
    rts

; Messages
.msg_loading:
    dc.b    "PART: Loading first partition...",13,10,0
.msg_no_part_found:
    dc.b    "PART: No valid partition found",13,10,0
.msg_found:
    dc.b    "PART: Partition found!",13,10,0
.msg_name:
    dc.b    "PART: Name: %s",13,10,0
.msg_cylinders:
    dc.b    "PART: Cylinders: %d to %d",13,10,0
.msg_start_lba:
    dc.b    "PART: Start LBA: %d",13,10,0
.msg_size:
    dc.b    "PART: Size: %d blocks",13,10,0
.msg_fs_start:
    dc.b    "PART: Filesystem starts at LBA %d",13,10,0
.msg_fs_size:
    dc.b    "PART: Filesystem size: %d blocks",13,10,0
.msg_dostype:
    dc.b    "PART: DosType: %x.l",13,10,0
.msg_next:
    dc.b    "PART: Next partition at block: %d",13,10,0
.msg_read_err:
    dc.b    "PART: Error reading block",13,10,0
    even
