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
    movem.l d1-d2/a0,-(sp)
    move.l  #$AA55AA55,d1       ; Test pattern

    ; Test for 2MB (ECS max)
    lea     $1FFFF0,a0
    move.l  (a0),d2             ; Save original
    move.l  d1,(a0)
    cmp.l   (a0),d1
    bne.s   .try_1mb
    move.l  d2,(a0)             ; Restore
    move.l  #$200000,d0         ; 2MB
    bra.s   .done

.try_1mb:
    ; Test for 1MB
    lea     $FFFF0,a0
    move.l  (a0),d2
    move.l  d1,(a0)
    cmp.l   (a0),d1
    bne.s   .default_512k
    move.l  d2,(a0)
    move.l  #$100000,d0         ; 1MB
    bra.s   .done

.default_512k:
    move.l  #$080000,d0         ; 512KB (minimum for Amiga)

.done:
    movem.l (sp)+,d1-d2/a0
    rts

; ============================================================
; DetectSlowRAM - Detect slow RAM (trapdoor expansion)
; Returns: d0.l = slow RAM size in bytes (0 if none)
; ============================================================
; Slow RAM starts at $C00000, typically 512KB on A500
; Maximum is 1.8MB but most expansions are 512KB
; ============================================================
DetectSlowRAM:
    movem.l d1-d2/a0,-(sp)

    ; Probe $C00000 for presence
    lea     SLOW_RAM_START,a0
    move.l  #$AA55AA55,d1
    move.l  (a0),d2             ; Save original
    move.l  d1,(a0)
    cmp.l   (a0),d1
    bne.s   .no_slow
    move.l  d2,(a0)             ; Restore

    ; Slow RAM detected, now size it
    ; Test 512KB boundaries up to 1.8MB
    move.l  #$080000,d0         ; Start with 512KB

.size_loop:
    cmp.l   #$180000,d0         ; Max 1.8MB
    bge.s   .done

    lea     SLOW_RAM_START,a0
    add.l   d0,a0
    move.l  (a0),d2
    move.l  d1,(a0)
    cmp.l   (a0),d1
    bne.s   .done
    move.l  d2,(a0)

    add.l   #$080000,d0         ; Next 512KB
    bra.s   .size_loop

.no_slow:
    moveq   #0,d0

.done:
    movem.l (sp)+,d1-d2/a0
    rts

; ============================================================
; DetectFastRAM - Detect fast RAM (Zorro expansion)
; Returns: d0.l = fast RAM size in bytes (0 if none)
; Side effect: Stores base address in FAST_RAM_BASE
; ============================================================
; Checks multiple locations:
; - $200000: Zorro II space (A500 edge connector, A2000/3000/4000)
; - $1000000: High memory (FS-UAE with A500+, Zorro III)
; ============================================================
DetectFastRAM:
    movem.l d1-d3/a0,-(sp)
    move.l  #$AA55AA55,d1

    ; Try Zorro II space first ($200000)
    lea     $200000,a0
    move.l  (a0),d2
    move.l  d1,(a0)
    cmp.l   (a0),d1
    bne.s   .try_high_mem
    move.l  d2,(a0)
    move.l  #$200000,d3         ; Base address
    bra.s   .size_it

.try_high_mem:
    ; Try high memory space ($1000000)
    lea     $1000000,a0
    move.l  (a0),d2
    move.l  d1,(a0)
    cmp.l   (a0),d1
    bne.s   .no_fast
    move.l  d2,(a0)
    move.l  #$1000000,d3        ; Base address

.size_it:
    ; Store base address
    move.l  d3,FAST_RAM_BASE

    ; Size the fast RAM in 1MB increments (faster for large blocks)
    move.l  #$1000,d0         ; Start with 1MB

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

    add.l   #$1000,d0         ; Next 1MB
    bra.s   .size_loop

.no_fast:
    clr.l   FAST_RAM_BASE
    moveq   #0,d0

.done:
    movem.l (sp)+,d1-d3/a0
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
; BuildMemoryMap - Create comprehensive memory map table
; ============================================================
; Memory map format (12 bytes per entry):
;   +0: Base address (long)
;   +4: Size in bytes (long)
;   +8: Type (word) - 0=Reserved, 1=Free, 2=ROM
;  +10: Flags (word) - bit 0: DMA capable
; ============================================================
BuildMemoryMap:
    movem.l d0-d1/a0,-(sp)
    lea     MEMMAP_TABLE,a0

    ; Entry 1: Reserved low chip RAM ($000-$3FFF)
    move.l  #$00000000,(a0)+    ; Base
    move.l  #KERNEL_CHIP,(a0)+  ; Size
    move.w  #MEM_TYPE_RESERVED,(a0)+ ; Type
    move.w  #$0001,(a0)+        ; Flags: DMA capable

    ; Entry 2: Free chip RAM ($4000 to detected size)
    move.l  #KERNEL_CHIP,(a0)+  ; Base
    move.l  CHIP_RAM_VAR,d0
    sub.l   #KERNEL_CHIP,d0
    move.l  d0,(a0)+            ; Size
    move.w  #MEM_TYPE_FREE,(a0)+ ; Type
    move.w  #$0001,(a0)+        ; Flags: DMA capable

    ; Entry 3: Slow RAM (if any)
    move.l  SLOW_RAM_VAR,d1
    tst.l   d1
    beq.s   .no_slow
    move.l  #SLOW_RAM_START,(a0)+ ; Base
    move.l  d1,(a0)+            ; Size
    move.w  #MEM_TYPE_FREE,(a0)+ ; Type
    move.w  #$0000,(a0)+        ; Flags: Not DMA capable

.no_slow:
    ; Entry 4: Fast RAM (if any)
    move.l  FAST_RAM_VAR,d1
    tst.l   d1
    beq.s   .no_fast
    move.l  FAST_RAM_BASE,(a0)+ ; Base (could be $200000 or $1000000)
    move.l  d1,(a0)+            ; Size
    move.w  #MEM_TYPE_FREE,(a0)+ ; Type
    move.w  #$0000,(a0)+        ; Flags: Not DMA capable

.no_fast:
    ; Entry 5: ROM
    move.l  #ROM_START,(a0)+
    move.l  #$040000,(a0)+      ; 256KB
    move.w  #MEM_TYPE_ROM,(a0)+
    move.w  #$0000,(a0)+

    ; Terminate with null entry
    move.l  #$00000000,(a0)+
    move.l  #$00000000,(a0)+
    move.w  #$0000,(a0)+
    move.w  #$0000,(a0)+

    movem.l (sp)+,d0-d1/a0
    rts

; ============================================================
; Data
; ============================================================
MemTestFailMsg:
    dc.b    "CHIP RAM TEST FAILED",10,13,0
    even
