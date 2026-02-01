; ============================================================
; partition.s - Rigid Disk Block (RDB) detection
; ============================================================
; Scans blocks 0-15 for RDB structure and displays information
; ============================================================

; ============================================================
; Constants
; ============================================================
RDB_BUFFER      equ $20000          ; Read buffer in chip RAM
RDB_MAGIC       equ $5244534B       ; "RDSK"
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
    bsr     IDERead
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
