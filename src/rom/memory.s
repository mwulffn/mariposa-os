; ============================================================
; memory.s - Memory detection and testing
; ============================================================
; Provides functions for detecting and testing chip/fast RAM
; ============================================================

; hardware.i already included by bootstrap.s

; ============================================================
; DetectChipRAM - Detect chip RAM size
; Returns: d0.l = chip RAM size in bytes
; ============================================================
DetectChipRAM:
    movem.l d1-d2/a0,-(sp)

    ; Test for 2MB (write to $1FFFF0)
    lea     $1FFFF0,a0
    move.l  #$AA55AA55,d1
    move.l  (a0),d2             ; Save original
    move.l  d1,(a0)
    cmp.l   (a0),d1
    bne.s   .try_1mb
    move.l  d2,(a0)             ; Restore
    move.l  #$200000,d0         ; 2MB
    bra.s   .done

.try_1mb:
    ; Test for 1MB (write to $FFFF0)
    lea     $FFFF0,a0
    move.l  (a0),d2             ; Save original
    move.l  d1,(a0)
    cmp.l   (a0),d1
    bne.s   .default_512k
    move.l  d2,(a0)             ; Restore
    move.l  #$100000,d0         ; 1MB
    bra.s   .done

.default_512k:
    move.l  #$080000,d0         ; 512KB

.done:
    movem.l (sp)+,d1-d2/a0
    rts

; ============================================================
; TestChipRAM - Quick test of chip RAM
; If failure: sets COLOR00=$FF0 (yellow) and halts
; ============================================================
TestChipRAM:
    movem.l d0-d2/a0-a1,-(sp)
    lea     CUSTOM,a1

    ; Get chip RAM size
    move.l  CHIP_RAM_VAR,d1

    ; Start at $4000 (after our data structures)
    move.l  #$4000,a0

.test_loop:
    ; Test this 4KB block
    move.l  #$AA55AA55,d2
    move.l  (a0),d0             ; Save original
    move.l  d2,(a0)
    cmp.l   (a0),d2
    bne.s   .fail
    not.l   d2
    move.l  d2,(a0)
    cmp.l   (a0),d2
    bne.s   .fail
    move.l  d0,(a0)             ; Restore

    ; Next 4KB block
    add.l   #$1000,a0
    cmp.l   d1,a0
    blt.s   .test_loop

    ; Success
    movem.l (sp)+,d0-d2/a0-a1
    rts

.fail:
    ; Set yellow screen and halt
    move.w  #$FF0,COLOR00(a1)
    lea     MemTestFailMsg(pc),a0
    bsr     SerialPutString
.halt:
    bra.s   .halt

; ============================================================
; DetectFastRAM - Detect fast RAM size
; Returns: d0.l = fast RAM size in bytes (0 if none)
; ============================================================
DetectFastRAM:
    movem.l d1-d2/a0,-(sp)

    ; Probe $200000 for presence
    lea     $200000,a0
    move.l  #$AA55AA55,d1
    move.l  (a0),d2             ; Save original (if any)
    move.l  d1,(a0)
    cmp.l   (a0),d1
    bne.s   .no_fast
    move.l  d2,(a0)             ; Restore

    ; Fast RAM detected, now size it
    ; Test 512KB boundaries up to 8MB
    move.l  #$080000,d0         ; Start with 512KB

.size_loop:
    cmp.l   #$800000,d0         ; Max 8MB
    bge.s   .done

    lea     $200000,a0
    add.l   d0,a0
    move.l  (a0),d2
    move.l  d1,(a0)
    cmp.l   (a0),d1
    bne.s   .done
    move.l  d2,(a0)

    add.l   #$080000,d0         ; Next 512KB
    bra.s   .size_loop

.no_fast:
    moveq   #0,d0

.done:
    movem.l (sp)+,d1-d2/a0
    rts

; ============================================================
; BuildMemoryMap - Create memory map table
; ============================================================
BuildMemoryMap:
    movem.l d0-d1/a0,-(sp)
    lea     MEMMAP_TABLE,a0

    ; Entry 1: Reserved area ($000-$3FFF)
    move.l  #$00000000,(a0)+    ; Base
    move.l  #$00004000,(a0)+    ; Size
    move.w  #MEM_TYPE_RESERVED,(a0)+ ; Type
    move.w  #$0000,(a0)+        ; Flags

    ; Entry 2: Free chip RAM ($4000 to detected size)
    move.l  #KERNEL_CHIP,(a0)+  ; Base
    move.l  CHIP_RAM_VAR,d0
    sub.l   #KERNEL_CHIP,d0
    move.l  d0,(a0)+            ; Size
    move.w  #MEM_TYPE_FREE,(a0)+ ; Type
    move.w  #$0000,(a0)+        ; Flags

    ; Entry 3: Fast RAM (if any)
    move.l  FAST_RAM_VAR,d1
    tst.l   d1
    beq.s   .no_fast
    move.l  #FAST_RAM_START,(a0)+ ; Base
    move.l  d1,(a0)+            ; Size
    move.w  #MEM_TYPE_FREE,(a0)+ ; Type
    move.w  #$0000,(a0)+        ; Flags

.no_fast:
    ; Entry 4: ROM
    move.l  #ROM_START,(a0)+
    move.l  #$040000,(a0)+      ; 256KB
    move.w  #MEM_TYPE_ROM,(a0)+
    move.w  #$0000,(a0)+

    ; Terminator
    move.l  #$00000000,(a0)+
    move.l  #$00000000,(a0)+
    move.w  #MEM_TYPE_END,(a0)+
    move.w  #$0000,(a0)+

    movem.l (sp)+,d0-d1/a0
    rts

; ============================================================
; Data
; ============================================================
MemTestFailMsg:
    dc.b    "CHIP RAM TEST FAILED - YELLOW SCREEN",10,13,0
    even
