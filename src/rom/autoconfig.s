; ============================================================
; autoconfig.s - Zorro II/III Expansion Autoconfig
; ============================================================
; Configures expansion cards in the Zorro bus
; Must be called before memory detection
; ============================================================

; hardware.i already included by bootstrap.s

; ============================================================
; ConfigureZorroII - Configure Zorro II expansion cards
; Returns: d0.l = base address for first memory card (or 0)
; ============================================================
; Zorro II autoconfig protocol:
; 1. Cards initially respond at $E80000
; 2. Read er_Type to identify card (or $FF if no card)
; 3. Read er_Flags to determine if memory or I/O
; 4. For memory cards: allocate base address starting at $200000
; 5. Write base address high/low bytes to ec_BaseAddress registers
; 6. Card relocates to new address and stops responding at $E80000
; 7. Repeat for next card
; ============================================================
ConfigureZorroII:
    movem.l d1-d6/a0-a2,-(sp)

    ; MAGENTA - Entering autoconfig
    lea     CUSTOM,a1
    move.w  #$F0F,COLOR00(a1)

    ; Print entry message
    lea     AutoconfigStartMsg(pc),a0
    bsr     SerialPutString

    move.l  #$200000,d3         ; Next allocation address
    moveq   #0,d0               ; First memory card base
    moveq   #8,d4               ; Safety counter - max 8 cards
    moveq   #0,d6               ; Previous er_Type (detect repeated reads)

.next_card:
    ; Check safety counter
    dbf     d4,.check_card
    lea     MaxCardsMsg(pc),a0
    bsr     SerialPutString
    bra   .done

.check_card:
    ; CYAN - Reading card
    move.w  #$0FF,COLOR00(a1)

    lea     ZORRO_BASE,a2       ; a2 = ZORRO_BASE (preserve through calls!)

    ; Read er_Type (register 00)
    move.l  d0,-(sp)            ; Save d0 (return value)
    moveq   #0,d1               ; Register offset 0
    bsr     ReadZorroReg
    move.b  d0,d1               ; d1 = er_Type
    move.l  (sp)+,d0            ; Restore d0 (return value)

    ; Check for no card (register 00 is NOT inverted, $FF = no card)
    cmp.b   #$FF,d1
    beq   .no_card

    ; Check for repeated/stuck value (same card appearing twice = stale data)
    cmp.b   d6,d1               ; Compare to previous er_Type
    beq   .no_card              ; Same value = stuck, treat as no card
    move.b  d1,d6               ; Save current for next iteration

    ; Check card type (bits 7-6): 11 = Zorro II
    move.b  d1,d2
    and.b   #$C0,d2             ; Mask bits 7-6
    cmp.b   #$C0,d2             ; Should be %11 for Zorro II
    bne   .no_card

    ; Save er_Type for size extraction later
    move.b  d1,-(sp)            ; Push er_Type on stack

    ; Read er_Flags (register 08)
    move.l  d0,-(sp)            ; Save d0 (return value)
    moveq   #8,d1               ; Register offset 8
    bsr     ReadZorroReg
    ; Registers except 00 are inverted
    eor.b   #$FF,d0             ; Invert to get logical value
    move.b  d0,d2               ; d2 = er_Flags
    move.l  (sp)+,d0            ; Restore d0 (return value)

    ; Check if memory board (bit 7)
    btst    #7,d2
    beq   .io_card

    ; YELLOW - Memory card found
    move.w  #$FF0,COLOR00(a1)

    lea     MemoryCardMsg(pc),a0
    bsr     SerialPutString

    ; Memory card - allocate at current address
    tst.l   d0
    bne.s   .not_first
    move.l  d3,d0               ; Save first memory base

.not_first:
    ; Print base address
    move.l  d0,-(sp)            ; Save d0 (return value)
    move.l  d3,-(sp)            ; Push address value
    pea     AllocatingAtFmt(pc)
    bsr     SerialPrintf
    addq.l  #8,sp               ; Clean up stack (format + 1 arg)
    move.l  (sp)+,d0            ; Restore d0 (return value)

    ; Write base address to card
    move.l  d3,d0               ; d0 = base address
    bsr     WriteBaseAddress

    ; ORANGE - Card relocated
    move.w  #$F80,COLOR00(a1)

    ; Get size code from saved er_Type
    moveq   #0,d1
    move.b  (sp)+,d1            ; Pop er_Type
    and.b   #$07,d1             ; Extract size bits 2-0

    ; Convert size code to bytes (Zorro II size encoding)
    ; 000=8MB, 001=64KB, 010=128KB, 011=256KB, 100=512KB, 101=1MB, 110=2MB, 111=4MB
    tst.b   d1
    bne.s   .not_8mb
    move.l  #$800000,d2         ; Size code 000 = 8MB special case
    bra.s   .got_size

.not_8mb:
    move.l  #$10000,d2          ; 64KB base
    subq.b  #1,d1               ; Adjust: 001->000, 010->001, etc
    lsl.l   d1,d2               ; Shift by (code-1)

.got_size:
    add.l   d2,d3               ; Advance allocation (d3 = next base)

    bra   .next_card

.io_card:
    ; Clean up stack (er_Type was pushed)
    addq.l  #2,sp               ; Pop the byte (aligned to word)

    ; RED - I/O card found
    move.w  #$F00,COLOR00(a1)
    lea     IOCardMsg(pc),a0
    bsr     SerialPutString
    ; I/O card - just shut it up for now
    move.b  #$FF,$4C(a2)        ; Write to shutup register (a2=ZORRO_BASE)
    bra   .next_card

.no_card:
    lea     NoCardMsg(pc),a0
    bsr     SerialPutString

.done:
    ; BLUE - Autoconfig done
    move.w  #$00F,COLOR00(a1)

    ; Print completion message
    move.l  d0,-(sp)            ; Save d0 (return value)
    move.l  d0,-(sp)            ; Push first RAM base address
    pea     AutoconfigDoneFmt(pc)
    bsr     SerialPrintf
    addq.l  #8,sp               ; Clean up stack (format + 1 arg)
    move.l  (sp)+,d0            ; Restore d0 (return value)

    movem.l (sp)+,d1-d6/a0-a2
    rts

; ============================================================
; ReadZorroReg - Read a Zorro II nibble-packed register
; Input:  a2 = ZORRO_BASE, d1 = register offset (0, 8, etc.)
; Output: d0.b = register value (combined nibbles)
; Clobbers: d1, d2
; ============================================================
ReadZorroReg:
    ; Read high nibble from offset+0
    move.w  0(a2,d1.w),d0       ; Read word at offset
    and.l   #$0000f000,d0
    rol.w   #4,d0               ; Rotate D15-D12 to D3-D0
    and.w   #$0F,d0             ; Mask high nibble (bits 7-4)
    lsl.b   #4,d0               ; Shift to bits 7-4

    ; Read low nibble from offset+2
    move.w  2(a2,d1.w),d2       ; Read word at offset+2
    and.l   #$0000f000,d2
    rol.w   #4,d2               ; Rotate D15-D12 to D3-D0
    and.w   #$0F,d2             ; Mask low nibble (bits 3-0)
    or.b    d2,d0               ; Combine: d0 = full register byte
    rts

; ============================================================
; WriteBaseAddress - Write base address to Zorro II card
; Input:  a2 = ZORRO_BASE, d0.l = base address
; Output: none
; Clobbers: d1, d2
; ============================================================
WriteBaseAddress:
    ; Write nibbles to config registers
    ; Each nibble goes in upper 4 bits of a BYTE write
    ; Extract and write A31-A24
    move.l  d0,d1
    lsr.l   #8,d1
    lsr.l   #8,d1
    lsr.l   #8,d1               ; d1 = A31-A24
    move.b  d1,d2
    and.b   #$F0,d2             ; High nibble (A31-A28)
    move.b  d2,$44(a2)
    move.b  d1,d2
    lsl.b   #4,d2               ; Low nibble (A27-A24) shifted to upper 4 bits
    move.b  d2,$46(a2)

    ; Extract and write A23-A16
    move.l  d0,d1
    lsr.l   #8,d1
    lsr.l   #8,d1               ; d1 = A23-A16
    move.b  d1,d2
    lsl.b   #4,d2               ; Low nibble (A19-A16) shifted to upper 4 bits
    move.b  d2,$4A(a2)
    move.b  d1,d2
    and.b   #$F0,d2             ; High nibble (A23-A20)
    move.b  d2,$48(a2)          ; TRIGGERS!
    rts

; ============================================================
; Data
; ============================================================
AutoconfigStartMsg:
    dc.b    "Autoconfig: Scanning Zorro bus...",10,13,0
    even

NoCardMsg:
    dc.b    "  No card found",10,13,0
    even

MemoryCardMsg:
    dc.b    "  Memory card found!",10,13,0
    even

AllocatingAtFmt:
    dc.b    "  Allocating at: $%x",10,13,0
    even

IOCardMsg:
    dc.b    "  I/O card found, shutting up",10,13,0
    even

MaxCardsMsg:
    dc.b    "  Max cards reached",10,13,0
    even

AutoconfigDoneFmt:
    dc.b    "Autoconfig: Done, first RAM base: $%x",10,13,0
    even

