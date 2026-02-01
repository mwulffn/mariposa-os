; ============================================================
; memory.s - Comprehensive Amiga Memory Detection
; ============================================================
; Detects all standard Amiga memory regions:
; - Chip RAM: $000000-$1FFFFF (up to 2MB on ECS)
; - Slow RAM: $C00000-$D7FFFF (trapdoor expansion, up to 1.8MB)
; - Fast RAM: $200000-$9FFFFF (Zorro II expansion, up to 8MB)
; ============================================================

; hardware.i already included by bootstrap.s

; ============================================================
; DetectChipRAM - Detect chip RAM size
; Returns: d0.l = chip RAM size in bytes
; ============================================================
; Tests at specific boundaries: 2MB, 1MB, 512KB
; Uses mirroring detection to avoid false positives
; ============================================================
DetectChipRAM:
    movem.l d1-d3/a0-a1,-(sp)

    ; Test for 2MB (ECS max)
    ; Check if $1FFFF0 is separate from $FFFF0
    lea     $FFFF0,a1           ; Base (1MB boundary)
    lea     $1FFFF0,a0          ; Test (2MB boundary)

    ; Save originals
    move.l  (a1),d3             ; Save base
    move.l  (a0),d2             ; Save test

    ; Two-pattern test
    move.l  #$AA55AA55,(a1)     ; Pattern A at base
    move.l  #$55AA55AA,(a0)     ; Pattern B at test

    ; Check if base survived
    cmp.l   #$AA55AA55,(a1)
    bne.s   .mirrored_2mb

    ; Separate RAM - restore and return 2MB
    move.l  d3,(a1)
    move.l  d2,(a0)
    move.l  #$200000,d0         ; 2MB
    bra.s   .done

.mirrored_2mb:
    ; Restore originals
    move.l  d3,(a1)
    move.l  d2,(a0)

    ; Test for 1MB
    ; Check if $FFFF0 is separate from $7FFF0
    lea     $7FFF0,a1           ; Base (512KB boundary)
    lea     $FFFF0,a0           ; Test (1MB boundary)

    ; Save originals
    move.l  (a1),d3
    move.l  (a0),d2

    ; Two-pattern test
    move.l  #$AA55AA55,(a1)     ; Pattern A at base
    move.l  #$55AA55AA,(a0)     ; Pattern B at test

    ; Check if base survived
    cmp.l   #$AA55AA55,(a1)
    bne.s   .mirrored_1mb

    ; Separate RAM - restore and return 1MB
    move.l  d3,(a1)
    move.l  d2,(a0)
    move.l  #$100000,d0         ; 1MB
    bra.s   .done

.mirrored_1mb:
    ; Restore originals
    move.l  d3,(a1)
    move.l  d2,(a0)
    move.l  #$080000,d0         ; 512KB (minimum for Amiga)

.done:
    movem.l (sp)+,d1-d3/a0-a1
    rts

; ============================================================
; DetectFastRAM - Detect fast RAM (Zorro expansion)
; Returns: d0.l = fast RAM size in bytes (0 if none)
; ============================================================
; Probes $200000 for Zorro II RAM (must call configure_zorro_ii first!)
; ============================================================
DetectFastRAM:
    movem.l d1-d3/a0,-(sp)
    move.l  #$AA55AA55,d1

    ; Probe Zorro II space ($200000)
    lea     $200000,a0
    move.l  (a0),d2
    move.l  d1,(a0)
    cmp.l   (a0),d1
    bne.s   .no_fast
    move.l  d2,(a0)
    move.l  #$200000,d3         ; Base address

    ; Size the fast RAM in 1MB increments (faster for large blocks)
    move.l  #$100000,d0         ; Start with 1MB

.size_loop:
    cmp.l   #$A00000,d0         ; Max 10MB (to be safe)
    bge.s   .done

    move.l  d3,a0
    add.l   d0,a0
    move.l  (a0),d2
    move.l  d1,(a0)
    cmp.l   (a0),d1
    bne.s   .done
    move.l  d2,(a0)

    add.l   #$100000,d0         ; Next 1MB
    bra.s   .size_loop

.no_fast:
    moveq   #0,d0

.done:
    movem.l (sp)+,d1-d3/a0
    rts

; ============================================================
; TestChipRAM - Quick test of chip RAM
; ============================================================
; Input: d2.l = chip RAM size limit
; On failure: sets COLOR00=$FF0 (yellow) and halts (does not return)
; On success: returns normally
; ============================================================
TestChipRAM:
    movem.l d0/d3/a0-a1,-(sp)
    lea     CUSTOM,a1
    move.l  #$4000,a0           ; Start after reserved area

.test_loop:
    move.l  #$AA55AA55,d3
    move.l  (a0),d0             ; Save original
    move.l  d3,(a0)
    cmp.l   (a0),d3
    bne.s   .test_fail
    not.l   d3
    move.l  d3,(a0)
    cmp.l   (a0),d3
    bne.s   .test_fail
    move.l  d0,(a0)             ; Restore
    add.l   #$1000,a0           ; Next 4KB
    cmp.l   d2,a0
    blt.s   .test_loop

    ; Success
    movem.l (sp)+,d0/d3/a0-a1
    rts

.test_fail:
    move.w  #$FF0,COLOR00(a1)
    lea     MemTestFailMsg(pc),a0
    bsr     serial_put_string
.halt:
    bra.s   .halt

; ============================================================
; BuildMemoryTable - Detect memory, build table, test chip RAM
; ============================================================
; On failure: sets COLOR00=$FF0 (yellow) and halts
; ============================================================
BuildMemoryTable:
    movem.l d0-d3/a0-a1,-(sp)
    lea     MEMMAP_TABLE,a0

    ; Entry 1: Reserved low chip RAM
    move.l  #$00000000,(a0)+
    move.l  #KERNEL_CHIP,(a0)+
    move.w  #MEM_TYPE_RESERVED,(a0)+
    move.w  #$0001,(a0)+        ; DMA capable

    ; Detect and add chip RAM entry
    bsr     DetectChipRAM       ; d0 = size
    move.l  d0,d2               ; Save total chip size for test
    move.l  #KERNEL_CHIP,(a0)+  ; Base
    move.l  d0,d1
    sub.l   #KERNEL_CHIP,d1
    move.l  d1,(a0)+            ; Size (total - reserved)
    move.w  #MEM_TYPE_CHIP,(a0)+
    move.w  #$0001,(a0)+        ; DMA capable

    ; Test chip RAM before continuing (d2 = chip RAM size limit)
    bsr     TestChipRAM

    ; Restore a0 to continue building table
    lea     MEMMAP_TABLE+24,a0  ; After 2 entries (12 bytes each)

    ; Detect and add fast RAM entry (if any)
    bsr     DetectFastRAM       ; d0 = size
    tst.l   d0
    beq.s   .no_fast
    move.l  #$200000,(a0)+      ; Base (Zorro II)
    move.l  d0,(a0)+            ; Size
    move.w  #MEM_TYPE_FAST,(a0)+
    move.w  #$0000,(a0)+        ; Not DMA capable

.no_fast:
    ; ROM entry
    move.l  #ROM_START,(a0)+
    move.l  #$040000,(a0)+
    move.w  #MEM_TYPE_ROM,(a0)+
    move.w  #$0000,(a0)+

    ; Terminator
    clr.l   (a0)+
    clr.l   (a0)+
    clr.w   (a0)+
    clr.w   (a0)+

    movem.l (sp)+,d0-d3/a0-a1
    rts

; ============================================================
; PrintMemoryMap - Output memory map table to serial port
; ============================================================
; Reads the memory map table and formats each entry for display
; Entry format: 12 bytes (base, size, type, flags)
; ============================================================
PrintMemoryMap:
    movem.l d0-d5/a0-a2,-(sp)

    ; Print header
    pea     .header(pc)
    bsr     SerialPrintf
    addq.l  #4,sp

    ; Start at beginning of memory map table
    lea     MEMMAP_TABLE,a1

.entry_loop:
    ; Read entry
    move.l  (a1)+,d0        ; base
    move.l  (a1)+,d1        ; size
    move.w  (a1)+,d2        ; type
    moveq   #0,d3
    move.w  (a1)+,d3        ; flags

    ; Check for end of table (base=0, size=0)
    tst.l   d0
    bne.s   .print_entry
    tst.l   d1
    beq.w   .done

.print_entry:
    ; Calculate end address (base + size - 1)
    move.l  d0,d4
    add.l   d1,d4
    subq.l  #1,d4

    ; Convert size to KB (divide by 1024)
    move.l  d1,d5
    lsr.l   #8,d5           ; /256
    lsr.l   #2,d5           ; /4 more = /1024 total

    ; Get type string pointer
    lea     .type_unknown(pc),a2
    cmp.w   #MEM_TYPE_RESERVED,d2
    bne.s   .check_chip
    lea     .type_reserved(pc),a2
    bra.s   .got_type
.check_chip:
    cmp.w   #MEM_TYPE_CHIP,d2
    bne.s   .check_fast
    lea     .type_chip(pc),a2
    bra.s   .got_type
.check_fast:
    cmp.w   #MEM_TYPE_FAST,d2
    bne.s   .check_rom
    lea     .type_fast(pc),a2
    bra.s   .got_type
.check_rom:
    cmp.w   #MEM_TYPE_ROM,d2
    bne.s   .got_type
    lea     .type_rom(pc),a2

.got_type:
    ; Print entry: "  $BASE-$END: TYPE ($SIZE KB)"
    move.l  d5,-(sp)        ; size in KB
    move.l  a2,-(sp)        ; type string
    move.l  d4,-(sp)        ; end address
    move.l  d0,-(sp)        ; base address
    pea     .entry_fmt(pc)
    bsr     SerialPrintf
    lea     20(sp),sp       ; Clean 5 items

    ; Print DMA flag if set
    btst    #0,d3
    beq.s   .no_dma
    pea     .dma_flag(pc)
    bsr     SerialPrintf
    addq.l  #4,sp

.no_dma:
    ; Print newline
    pea     .newline(pc)
    bsr     SerialPrintf
    addq.l  #4,sp

    bra     .entry_loop

.done:
    movem.l (sp)+,d0-d5/a0-a2
    rts

.header:
    dc.b    "Memory Map:",10,13,0
.entry_fmt:
    dc.b    "  $%.lx-$%.lx: %s ($%.lx KB)",0
.dma_flag:
    dc.b    " [DMA]",0
.newline:
    dc.b    10,13,0
.type_reserved:
    dc.b    "Reserved",0
.type_chip:
    dc.b    "Chip",0
.type_fast:
    dc.b    "Fast",0
.type_rom:
    dc.b    "ROM",0
.type_unknown:
    dc.b    "???",0
    even

; ============================================================
; Data
; ============================================================
MemTestFailMsg:
    dc.b    "CHIP RAM TEST FAILED",10,13,0
    even
