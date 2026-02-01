; filesystem.s - FAT16 filesystem implementation
; Loads SYSTEM.BIN from a FAT16 partition to $200000

; ============================================================================
; Constants
; ============================================================================

; Buffer locations (after existing buffers at $20000, $21000)
FS_BOOT_BUFFER      equ $22000      ; Boot sector (512 bytes)
FS_FAT_BUFFER       equ $22200      ; FAT sector cache (512 bytes)
FS_DIR_BUFFER       equ $22400      ; Directory sector buffer (512 bytes)
FS_VARS             equ $23000      ; Filesystem variables

; Target address for kernel
KERNEL_LOAD_ADDR    equ $200000

; BPB offsets (little-endian fields in boot sector)
BPB_BYTES_PER_SEC   equ 11          ; word
BPB_SEC_PER_CLUS    equ 13          ; byte
BPB_RSVD_SEC_CNT    equ 14          ; word
BPB_NUM_FATS        equ 16          ; byte
BPB_ROOT_ENT_CNT    equ 17          ; word
BPB_FAT_SIZE_16     equ 22          ; word
BPB_SIGNATURE       equ 510         ; 0x55AA (little-endian)

; Directory entry offsets
DIR_NAME            equ 0           ; 11 bytes (8.3 format)
DIR_ATTR            equ 11          ; byte
DIR_FI_CLUSTER      equ 26          ; word (little-endian)
DIR_FILE_SIZE       equ 28          ; long (little-endian)
DIR_ENTRY_SIZE      equ 32

; FAT16 special values
FAT16_EOF_MIN       equ $FFF8

; FS_VARS structure offsets
FSV_PARTITION_LBA   equ 0           ; long
FSV_BYTES_PER_SEC   equ 4           ; word
FSV_SEC_PER_CLUS    equ 6           ; byte
FSV_RESERVED_SEC    equ 8           ; word
FSV_NUM_FATS        equ 10          ; byte
FSV_ROOT_ENT_CNT    equ 12          ; word
FSV_FAT_SIZE        equ 14          ; word
FSV_ROOT_DIR_START  equ 16          ; word
FSV_ROOT_DIR_SECS   equ 18          ; word
FSV_DATA_START_SEC  equ 20          ; long
FSV_CACHED_FAT_SEC  equ 24          ; long (-1 if none)

; ============================================================================
; LoadSystemBin - Main entry point
; ============================================================================
; Input:
;   D1.l = partition start LBA
;   D2.l = partition size in sectors
; Output:
;   D0.l = 0 on success, -1 on error
;   D1.l = file size in bytes (on success)
; ============================================================================
LoadSystemBin:
    movem.l d2-d7/a0-a6,-(sp)

    ; Print entry message
    pea     .msg_entry(pc)
    bsr     SerialPrintf
    addq.l  #4,sp

    ; Initialize filesystem
    bsr     FAT16Init
    tst.l   d0
    bne     .error

    ; Search for SYSTEM.BIN
    lea     .filename(pc),a0
    bsr     FAT16FindFile
    tst.l   d0
    bne     .error

    ; D1 = starting cluster, D2 = file size
    move.l  d1,d3               ; d3 = current cluster
    move.l  d2,d4               ; d4 = remaining bytes
    move.l  d2,d5               ; d5 = total file size (for return)
    lea     KERNEL_LOAD_ADDR,a2 ; a2 = destination pointer

    ; Check file size limit (512KB)
    cmp.l   #$80000,d4
    bhi     .file_too_large

    pea     .msg_loading(pc)
    bsr     SerialPrintf
    addq.l  #4,sp

.read_loop:
    ; Read current cluster
    move.l  a2,a0               ; destination
    move.l  d3,d0               ; cluster number
    bsr     FAT16ReadCluster
    tst.l   d0
    bne     .error

    ; Calculate bytes read
    lea     FS_VARS,a0
    moveq   #0,d0
    move.b  FSV_SEC_PER_CLUS(a0),d0
    lsl.w   #8,d0               ; multiply by 512 (assuming 512 bytes/sector)
    lsl.w   #1,d0

    ; Update pointers and counters
    add.l   d0,a2               ; advance destination
    sub.l   d0,d4               ; decrease remaining bytes
    ble     .done               ; if <= 0, we're done

    ; Get next cluster
    move.l  d3,d0
    bsr     FAT16GetNextCluster
    tst.l   d0
    bmi     .error

    move.l  d0,d3               ; update current cluster

    ; Check for EOF
    cmp.w   #FAT16_EOF_MIN,d3
    bhs     .done

    bra     .read_loop

.done:
    move.l  d5,-(sp)            ; file size
    pea     .msg_success(pc)
    bsr     SerialPrintf
    addq.l  #8,sp

    moveq   #0,d0               ; success
    move.l  d5,d1               ; return file size
    movem.l (sp)+,d2-d7/a0-a6
    rts

.file_too_large:
    pea     .msg_too_large(pc)
    bsr     SerialPrintf
    addq.l  #4,sp
    moveq   #-1,d0
    movem.l (sp)+,d2-d7/a0-a6
    rts

.error:
    moveq   #-1,d0
    movem.l (sp)+,d2-d7/a0-a6
    rts

.msg_entry:
    dc.b    'FAT16: LoadSystemBin called',13,10,0
    even

.filename:
    dc.b    'SYSTEM  BIN',0
    even

.msg_loading:
    dc.b    'FAT16: Loading file...',13,10,0
    even

.msg_success:
    dc.b    'FAT16: Loaded %x.l bytes',13,10,0
    even

.msg_too_large:
    dc.b    'FAT16: ERROR - File too large (>512KB)',13,10,0
    even

; ============================================================================
; FAT16Init - Parse boot sector and initialize filesystem
; ============================================================================
; Input:
;   D1.l = partition start LBA
;   D2.l = partition size
; Output:
;   D0.l = 0 on success, -1 on error
; ============================================================================
FAT16Init:
    movem.l d1-d7/a0-a6,-(sp)

    pea     .msg_init(pc)
    bsr     SerialPrintf
    addq.l  #4,sp

    ; Save partition LBA
    lea     FS_VARS,a3
    move.l  d1,FSV_PARTITION_LBA(a3)

    ; Initialize cached FAT sector to -1
    move.l  #-1,FSV_CACHED_FAT_SEC(a3)

    ; Read boot sector (LBA 0 of partition)
    lea     FS_BOOT_BUFFER,a0
    moveq   #1,d2               ; 1 sector
    ; d1 already contains partition LBA
    bsr     IDERead
    tst.l   d0
    bne     .read_error

    ; Validate boot signature (0x55 at 510, 0xAA at 511)
    lea     FS_BOOT_BUFFER,a0
    cmp.b   #$55,BPB_SIGNATURE(a0)
    bne     .invalid_sig
    cmp.b   #$AA,BPB_SIGNATURE+1(a0)
    bne     .invalid_sig

    ; Parse BPB fields (little-endian -> big-endian)

    ; Bytes per sector (word at offset 11)
    move.b  BPB_BYTES_PER_SEC+1(a0),d0
    lsl.w   #8,d0
    move.b  BPB_BYTES_PER_SEC(a0),d0
    move.w  d0,FSV_BYTES_PER_SEC(a3)

    ; Sectors per cluster (byte at offset 13)
    move.b  BPB_SEC_PER_CLUS(a0),FSV_SEC_PER_CLUS(a3)

    ; Reserved sectors (word at offset 14)
    move.b  BPB_RSVD_SEC_CNT+1(a0),d0
    lsl.w   #8,d0
    move.b  BPB_RSVD_SEC_CNT(a0),d0
    move.w  d0,FSV_RESERVED_SEC(a3)

    ; Number of FATs (byte at offset 16)
    move.b  BPB_NUM_FATS(a0),FSV_NUM_FATS(a3)

    ; Root entry count (word at offset 17)
    move.b  BPB_ROOT_ENT_CNT+1(a0),d0
    lsl.w   #8,d0
    move.b  BPB_ROOT_ENT_CNT(a0),d0
    move.w  d0,FSV_ROOT_ENT_CNT(a3)

    ; FAT size in sectors (word at offset 22)
    move.b  BPB_FAT_SIZE_16+1(a0),d0
    lsl.w   #8,d0
    move.b  BPB_FAT_SIZE_16(a0),d0
    move.w  d0,FSV_FAT_SIZE(a3)

    ; Calculate derived values
    ; RootDirStart = ReservedSectors + (NumFATs * SectorsPerFAT)
    moveq   #0,d0
    move.b  FSV_NUM_FATS(a3),d0
    moveq   #0,d1
    move.w  FSV_FAT_SIZE(a3),d1
    mulu    d1,d0                   ; d0 = NumFATs * FATSize
    moveq   #0,d1
    move.w  FSV_RESERVED_SEC(a3),d1
    add.l   d1,d0                   ; d0 = RootDirStart
    move.w  d0,FSV_ROOT_DIR_START(a3)

    ; RootDirSectors = (RootEntries * 32 + 511) / 512
    moveq   #0,d0
    move.w  FSV_ROOT_ENT_CNT(a3),d0
    lsl.l   #5,d0                   ; multiply by 32
    add.l   #511,d0
    lsr.l   #8,d0                   ; divide by 512
    lsr.l   #1,d0
    move.w  d0,FSV_ROOT_DIR_SECS(a3)

    ; DataStart = RootDirStart + RootDirSectors
    moveq   #0,d0
    move.w  FSV_ROOT_DIR_START(a3),d0
    moveq   #0,d1
    move.w  FSV_ROOT_DIR_SECS(a3),d1
    add.l   d1,d0
    move.l  d0,FSV_DATA_START_SEC(a3)

    ; Print debug info
    moveq   #0,d1
    move.b  FSV_SEC_PER_CLUS(a3),d1
    move.l  d1,-(sp)
    moveq   #0,d0
    move.w  FSV_BYTES_PER_SEC(a3),d0
    move.l  d0,-(sp)
    pea     .msg_bps(pc)
    bsr     SerialPrintf
    lea     12(sp),sp

    moveq   #0,d2
    move.w  FSV_FAT_SIZE(a3),d2
    move.l  d2,-(sp)
    moveq   #0,d1
    move.b  FSV_NUM_FATS(a3),d1
    move.l  d1,-(sp)
    moveq   #0,d0
    move.w  FSV_RESERVED_SEC(a3),d0
    move.l  d0,-(sp)
    pea     .msg_reserved(pc)
    bsr     SerialPrintf
    lea     16(sp),sp

    moveq   #0,d1
    move.w  FSV_ROOT_DIR_START(a3),d1
    move.l  d1,-(sp)
    moveq   #0,d0
    move.w  FSV_ROOT_ENT_CNT(a3),d0
    move.l  d0,-(sp)
    pea     .msg_rootent(pc)
    bsr     SerialPrintf
    lea     12(sp),sp

    move.l  FSV_DATA_START_SEC(a3),-(sp)
    pea     .msg_datastart(pc)
    bsr     SerialPrintf
    addq.l  #8,sp

    moveq   #0,d0               ; success
    movem.l (sp)+,d1-d7/a0-a6
    rts

.invalid_sig:
    pea     .msg_invalid(pc)
    bsr     SerialPrintf
    addq.l  #4,sp
    moveq   #-1,d0
    movem.l (sp)+,d1-d7/a0-a6
    rts

.read_error:
    pea     .msg_readerror(pc)
    bsr     SerialPrintf
    addq.l  #4,sp
    moveq   #-1,d0
    movem.l (sp)+,d1-d7/a0-a6
    rts

.msg_init:
    dc.b    'FAT16: Initializing filesystem...',13,10,0
    even

.msg_bps:
    dc.b    'FAT16: Bytes/sector: %x.l, Sec/cluster: %x.l',13,10,0
    even

.msg_reserved:
    dc.b    'FAT16: Reserved: %x.l, FATs: %x.l, FAT size: %x.l',13,10,0
    even

.msg_rootent:
    dc.b    'FAT16: Root entries: %x.l, Root start: %x.l',13,10,0
    even

.msg_datastart:
    dc.b    'FAT16: Data starts at sector %x.l',13,10,0
    even

.msg_invalid:
    dc.b    'FAT16: ERROR - Invalid boot signature',13,10,0
    even

.msg_readerror:
    dc.b    'FAT16: ERROR - Failed to read boot sector',13,10,0
    even

; ============================================================================
; FAT16FindFile - Search root directory for a file
; ============================================================================
; Input:
;   A0 = pointer to 11-byte filename (e.g., "SYSTEM  BIN")
; Output:
;   D0.l = 0 on success, -1 on error
;   D1.l = starting cluster (on success)
;   D2.l = file size in bytes (on success)
; ============================================================================
FAT16FindFile:
    movem.l d3-d7/a0-a6,-(sp)

    move.l  a0,a4               ; save filename pointer

    pea     .msg_search(pc)
    bsr     SerialPrintf
    addq.l  #4,sp

    lea     FS_VARS,a3
    moveq   #0,d7
    move.w  FSV_ROOT_DIR_SECS(a3),d7    ; d7 = sectors to scan
    moveq   #0,d6
    move.w  FSV_ROOT_DIR_START(a3),d6   ; d6 = current sector offset

.sector_loop:
    ; Read root directory sector
    lea     FS_DIR_BUFFER,a0
    move.l  FSV_PARTITION_LBA(a3),d1
    add.l   d6,d1                       ; LBA = partition + root_start + offset
    moveq   #1,d2                       ; 1 sector
    bsr     IDERead
    tst.l   d0
    bne     .read_error

    ; Scan entries in this sector (16 entries per 512-byte sector)
    lea     FS_DIR_BUFFER,a5
    moveq   #16-1,d5                    ; 16 entries per sector

.entry_loop:
    ; Check first byte
    move.b  DIR_NAME(a5),d0
    beq     .not_found              ; 0x00 = end of directory
    cmp.b   #$E5,d0
    beq     .next_entry             ; 0xE5 = deleted entry

    ; Check attributes - skip volume labels and long filenames
    move.b  DIR_ATTR(a5),d0
    andi.b  #$08,d0                 ; volume label bit
    bne     .next_entry
    move.b  DIR_ATTR(a5),d0
    andi.b  #$0F,d0
    cmp.b   #$0F,d0                 ; long filename entry
    beq     .next_entry

    ; Compare filename (11 bytes)
    move.l  a4,a0                   ; filename to find
    move.l  a5,a1                   ; directory entry name
    moveq   #11-1,d4

.cmp_loop:
    move.b  (a0)+,d0
    move.b  (a1)+,d1
    cmp.b   d0,d1
    bne     .next_entry
    dbf     d4,.cmp_loop

    ; Found it!
    pea     .msg_found(pc)
    bsr     SerialPrintf
    addq.l  #4,sp

    ; Extract starting cluster (little-endian word at offset 26)
    moveq   #0,d1
    move.b  DIR_FI_CLUSTER+1(a5),d1
    lsl.w   #8,d1
    move.b  DIR_FI_CLUSTER(a5),d1

    ; Extract file size (little-endian long at offset 28)
    move.b  DIR_FILE_SIZE+3(a5),d2
    lsl.l   #8,d2
    move.b  DIR_FILE_SIZE+2(a5),d2
    lsl.l   #8,d2
    move.b  DIR_FILE_SIZE+1(a5),d2
    lsl.l   #8,d2
    move.b  DIR_FILE_SIZE(a5),d2

    ; Print cluster and size
    move.l  d2,-(sp)            ; size
    move.l  d1,-(sp)            ; cluster
    pea     .msg_found_info(pc)
    bsr     SerialPrintf
    lea     12(sp),sp

    moveq   #0,d0               ; success
    movem.l (sp)+,d3-d7/a0-a6
    rts

.next_entry:
    lea     DIR_ENTRY_SIZE(a5),a5
    dbf     d5,.entry_loop

    ; Move to next sector
    addq.l  #1,d6
    subq.l  #1,d7
    bne     .sector_loop

.not_found:
    pea     .msg_notfound(pc)
    bsr     SerialPrintf
    addq.l  #4,sp
    moveq   #-1,d0
    movem.l (sp)+,d3-d7/a0-a6
    rts

.read_error:
    pea     .msg_readerror(pc)
    bsr     SerialPrintf
    addq.l  #4,sp
    moveq   #-1,d0
    movem.l (sp)+,d3-d7/a0-a6
    rts

.msg_search:
    dc.b    'FAT16: Searching for SYSTEM.BIN...',13,10,0
    even

.msg_found:
    dc.b    'FAT16: Found! ',0
    even

.msg_found_info:
    dc.b    'Cluster: %x.l, Size: %x.l bytes',13,10,0
    even

.msg_notfound:
    dc.b    'FAT16: ERROR - File not found',13,10,0
    even

.msg_readerror:
    dc.b    'FAT16: ERROR - Failed to read directory',13,10,0
    even

; ============================================================================
; FAT16ReadCluster - Read a cluster to memory
; ============================================================================
; Input:
;   A0 = destination buffer
;   D0.l = cluster number
; Output:
;   D0.l = 0 on success, -1 on error
; ============================================================================
FAT16ReadCluster:
    movem.l d1-d7/a0-a6,-(sp)

    move.l  a0,a4               ; save destination
    move.l  d0,d4               ; save cluster number

    ; Convert cluster to LBA
    ; LBA = PartitionLBA + DataStart + (Cluster - 2) * SecPerCluster
    lea     FS_VARS,a3

    sub.l   #2,d4               ; cluster - 2
    moveq   #0,d0
    move.b  FSV_SEC_PER_CLUS(a3),d0
    mulu    d0,d4               ; (cluster - 2) * sec_per_cluster

    move.l  FSV_DATA_START_SEC(a3),d1
    add.l   d4,d1               ; + data_start
    move.l  FSV_PARTITION_LBA(a3),d4
    add.l   d4,d1               ; + partition_lba

    ; Read sectors
    move.l  a4,a0               ; destination
    moveq   #0,d2
    move.b  FSV_SEC_PER_CLUS(a3),d2     ; number of sectors
    bsr     IDERead
    tst.l   d0
    bne     .error

    moveq   #0,d0               ; success
    movem.l (sp)+,d1-d7/a0-a6
    rts

.error:
    pea     .msg_error(pc)
    bsr     SerialPrintf
    addq.l  #4,sp
    moveq   #-1,d0
    movem.l (sp)+,d1-d7/a0-a6
    rts

.msg_error:
    dc.b    'FAT16: ERROR - Failed to read cluster',13,10,0
    even

; ============================================================================
; FAT16GetNextCluster - Get next cluster from FAT chain
; ============================================================================
; Input:
;   D0.l = current cluster number
; Output:
;   D0.l = next cluster number (or -1 on error)
; ============================================================================
FAT16GetNextCluster:
    movem.l d1-d7/a0-a6,-(sp)

    move.l  d0,d4               ; save cluster number

    ; Calculate FAT sector and offset
    ; FAT entry is 2 bytes, so byte offset = cluster * 2
    ; FAT sector = (cluster * 2) / 512
    ; Offset in sector = (cluster * 2) % 512

    move.l  d4,d0
    lsl.l   #1,d0               ; cluster * 2 = byte offset in FAT

    move.l  d0,d5               ; save byte offset
    lsr.l   #8,d0               ; divide by 512
    lsr.l   #1,d0               ; d0 = FAT sector number (relative to FAT start)

    move.l  d5,d6
    andi.l  #$1FF,d6            ; d6 = offset within sector (modulo 512)

    ; Calculate absolute LBA of FAT sector
    lea     FS_VARS,a3
    moveq   #0,d1
    move.w  FSV_RESERVED_SEC(a3),d1
    add.l   d1,d0               ; FAT sector offset from partition start
    move.l  FSV_PARTITION_LBA(a3),d1
    add.l   d1,d0               ; d0 = absolute LBA

    ; Check if this sector is already cached
    cmp.l   FSV_CACHED_FAT_SEC(a3),d0
    beq     .cached

    ; Read FAT sector
    move.l  d0,FSV_CACHED_FAT_SEC(a3)   ; update cache tag
    move.l  d0,d1
    lea     FS_FAT_BUFFER,a0
    moveq   #1,d2
    bsr     IDERead
    tst.l   d0
    bne     .error

.cached:
    ; Read the FAT entry (little-endian word)
    lea     FS_FAT_BUFFER,a0
    add.l   d6,a0               ; pointer to entry
    moveq   #0,d0
    move.b  1(a0),d0            ; high byte
    lsl.w   #8,d0
    move.b  (a0),d0             ; low byte

    movem.l (sp)+,d1-d7/a0-a6
    rts

.error:
    pea     .msg_error(pc)
    bsr     SerialPrintf
    addq.l  #4,sp
    moveq   #-1,d0
    movem.l (sp)+,d1-d7/a0-a6
    rts

.msg_error:
    dc.b    'FAT16: ERROR - Failed to read FAT',13,10,0
    even
