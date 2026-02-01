; ============================================================
; autoconfig.s - Zorro II/III Expansion Autoconfig
; ============================================================
; Configures expansion cards in the Zorro bus
; Must be called before memory detection
; ============================================================

; hardware.i already included by bootstrap.s

; ============================================================
; configure_zorro_ii - Configure Zorro II expansion cards
; Returns: d0.l = base address for first memory card (or 0)
; Clobbers: d2-d6, a2, a6
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
configure_zorro_ii:
    movem.l d2-d6/a2/a6,-(sp)

    ; MAGENTA - Entering autoconfig
    lea     CUSTOM,a6
    move.w  #$F0F,COLOR00(a6)

    ; Print entry message
    lea     autoconfig_start_msg(pc),a0
    bsr     serial_put_string

    move.l  #$200000,d3         ; Next allocation address
    moveq   #0,d5               ; First memory card base
    moveq   #8,d4               ; Safety counter - max 8 cards
    moveq   #0,d6               ; Previous er_Type (detect repeated reads)
    lea     ZORRO_BASE,a2       ; a2 = current autoconfig slot

.next_card:
    ; Check safety counter
    dbf     d4,.check_card
    lea     max_cards_msg(pc),a0
    bsr     serial_put_string
    bra   .done

.check_card:
    ; CYAN - Reading card
    move.w  #$0FF,COLOR00(a6)

    ; a2 already points to current slot (preserved through calls)

    ; Read er_Type (register 00)
    moveq   #0,d1               ; Register offset 0
    bsr     read_zorro_reg
    move.b  d0,d1               ; d1 = er_Type

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
    moveq   #8,d1               ; Register offset 8
    bsr     read_zorro_reg
    ; Registers except 00 are inverted
    eor.b   #$FF,d0             ; Invert to get logical value
    move.b  d0,d2               ; d2 = er_Flags

    ; Check if memory board (bit 7)
    btst    #7,d2
    beq   .io_card

    ; YELLOW - Memory card found
    move.w  #$FF0,COLOR00(a6)

    lea     memory_card_msg(pc),a0
    bsr     serial_put_string

    ; Memory card - allocate at current address
    tst.l   d5
    bne.s   .not_first
    move.l  d3,d5               ; Save first memory base

.not_first:
    ; Print base address
    move.l  d3,-(sp)            ; Push address value
    pea     allocating_at_fmt(pc)
    bsr     SerialPrintf
    addq.l  #8,sp               ; Clean up stack (format + 1 arg)

    ; Write base address to card
    move.l  d3,d0               ; d0 = base address
    bsr     write_base_address

    ; ORANGE - Card relocated
    move.w  #$F80,COLOR00(a6)

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
    add.l   #$10000,a2          ; Advance to next autoconfig slot

    bra   .next_card

.io_card:
    ; Clean up stack (er_Type was pushed)
    addq.l  #2,sp               ; Pop the byte (aligned to word)

    ; RED - I/O card found
    move.w  #$F00,COLOR00(a6)
    lea     io_card_msg(pc),a0
    bsr     serial_put_string
    ; I/O card - just shut it up for now
    move.b  #$FF,$4C(a2)        ; Write to shutup register
    add.l   #$10000,a2          ; Advance to next autoconfig slot
    bra   .next_card

.no_card:
    lea     no_card_msg(pc),a0
    bsr     serial_put_string

.done:
    ; BLUE - Autoconfig done
    move.w  #$00F,COLOR00(a6)

    ; Print completion message
    move.l  d5,-(sp)            ; Push first RAM base address
    pea     autoconfig_done_fmt(pc)
    bsr     SerialPrintf
    addq.l  #8,sp               ; Clean up stack (format + 1 arg)

    move.l  d5,d0               ; Return first RAM base in d0
    movem.l (sp)+,d2-d6/a2/a6
    rts

; ============================================================
; read_zorro_reg - Read a Zorro II nibble-packed register
; ============================================================
; Input:  a2 = ZORRO_BASE, d1.w = register offset (0, 8, etc.)
; Output: d0.b = register value (combined nibbles)
; Clobbers: d1
; ============================================================
read_zorro_reg:
    ; Read high nibble from offset+0
    move.w  0(a2,d1.w),d0       ; Read word at offset
    addq.w  #2,d1               ; Advance to offset+2
    and.l   #$0000f000,d0
    rol.w   #4,d0
    and.w   #$0F,d0
    lsl.b   #4,d0               ; High nibble in bits 7-4

    ; Read low nibble from offset+2
    move.w  0(a2,d1.w),d1       ; Reuse d1 for low nibble
    and.l   #$0000f000,d1
    rol.w   #4,d1
    and.w   #$0F,d1
    or.b    d1,d0               ; Combine nibbles
    rts

; ============================================================
; write_base_address - Write base address to Zorro II card
; ============================================================
; Input:  a2 = ZORRO_BASE, d0.l = base address
; Clobbers: d1
; ============================================================
write_base_address:
    move.l  d2,-(sp)            ; Save callee-save register
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
    move.l  (sp)+,d2            ; Restore callee-save register
    rts

; ============================================================
; Data
; ============================================================
autoconfig_start_msg:
    dc.b    "Autoconfig: Scanning Zorro bus...",10,13,0
    even

no_card_msg:
    dc.b    "  No card found",10,13,0
    even

memory_card_msg:
    dc.b    "  Memory card found!",10,13,0
    even

allocating_at_fmt:
    dc.b    "  Allocating at: $%x",10,13,0
    even

io_card_msg:
    dc.b    "  I/O card found, shutting up",10,13,0
    even

max_cards_msg:
    dc.b    "  Max cards reached",10,13,0
    even

autoconfig_done_fmt:
    dc.b    "Autoconfig: Done, first RAM base: $%x",10,13,0
    even

