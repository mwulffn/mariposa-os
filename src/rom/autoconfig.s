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
    movem.l d1-d5/a0-a1,-(sp)

    ; MAGENTA - Entering autoconfig
    lea     CUSTOM,a1
    move.w  #$F0F,COLOR00(a1)

    ; Print entry message
    lea     AutoconfigStartMsg(pc),a0
    bsr     SerialPutString

    move.l  #$200000,d3         ; Next allocation address
    moveq   #0,d0               ; First memory card base
    moveq   #8,d4               ; Safety counter - max 8 cards

.next_card:
    ; Check safety counter
    dbf     d4,.check_card
    lea     MaxCardsMsg(pc),a0
    bsr     SerialPutString
    bra   .done

.check_card:
    ; CYAN - Reading card
    move.w  #$0FF,COLOR00(a1)

    lea     ZORRO_BASE,a0

    ; Read er_Type (register 00) using proper nibble protocol
    ; Zorro II: nibbles in D15-D12 of words at $E80000 and $E80002
    move.w  (a0),d1             ; Read $E80000
    and.l   #$0000f000,d1
    rol.w   #4,d1               ; Rotate D15-D12 to D3-D0
    and.w   #$0F,d1             ; Mask high nibble (bits 7-4)
    lsl.b   #4,d1               ; Shift to bits 7-4

    move.w  2(a0),d2            ; Read $E80002
    and.l   #$0000f000,d2
    rol.w   #4,d2               ; Rotate D15-D12 to D3-D0
    and.w   #$0F,d2             ; Mask low nibble (bits 3-0)
    or.b    d2,d1               ; Combine: d1 = full er_Type byte

    ; Debug: print what we read
    move.l  d1,-(sp)
    lea     ReadingCardMsg(pc),a0
    bsr     SerialPutString
    moveq   #0,d0
    move.b  3(sp),d0
    bsr     SerialPutHex
    lea     NewlineMsg(pc),a0
    bsr     SerialPutString
    move.l  (sp)+,d1

    ; Check for no card (register 00 is NOT inverted, $FF = no card)
    cmp.b   #$FF,d1
    beq   .no_card

    ; Check card type (bits 7-6): 11 = Zorro II
    move.b  d1,d2
    and.b   #$C0,d2             ; Mask bits 7-6
    cmp.b   #$C0,d2             ; Should be %11 for Zorro II
    bne   .no_card

    ; Save er_Type for size extraction later
    move.b  d1,-(sp)            ; Push er_Type on stack

    lea     ZORRO_BASE,a0

    ; Read er_Flags (register 08) using nibble protocol
    move.w  $08(a0),d1          ; Read $E80008 (high nibble)
    rol.w   #4,d1
    and.w   #$0F,d1
    lsl.b   #4,d1

    move.w  $0A(a0),d2          ; Read $E8000A (low nibble)
    rol.w   #4,d2
    and.w   #$0F,d2
    or.b    d2,d1               ; d1 = full er_Flags byte

    ; Registers except 00 are inverted
    eor.b   #$FF,d1             ; Invert to get logical value
    move.b  d1,d2               ; d2 = er_Flags

    ; Check if memory board (bit 7)
    btst    #7,d2
    beq   .io_card

    ; YELLOW - Memory card found
    move.w  #$FF0,COLOR00(a1)

    ; Debug: memory card found
    lea     MemoryCardMsg(pc),a0
    bsr     SerialPutString

    ; Memory card - allocate at current address
    tst.l   d0
    bne.s   .not_first
    move.l  d3,d0               ; Save first memory base

.not_first:
    ; Debug: print base address
    lea     AllocatingAtMsg(pc),a0
    bsr     SerialPutString
    move.l  d3,d0
    bsr     SerialPutHex
    lea     NewlineMsg(pc),a0
    bsr     SerialPutString

    ; Write base address using nibble protocol
    ; Each nibble goes in bits 15-12 of a word write
    ; For Zorro II: write $44/$46 (A31-A24) then $48/$4A (A23-A16, triggers)
    move.l  d3,d1               ; d1 = base address ($00200000)

    ; Debug: show what we're writing
    lea     WritingBaseMsg(pc),a0
    bsr     SerialPutString

    ; 0000 0000 0010 0000 0000 0000 0000 0000 (200000)
    lea     ZORRO_BASE,a0
    
    move.b #$00,$46(a0)
    move.b #$00,$44(a0)
    move.b #$00,$4A(a0)
    move.b #%0010,$48(a0)
    ; Write A31-A24 (byte $00) as nibbles
    ; move.l  d1,d2
    ;lsr.l   #8,d2
    ;lsr.l   #8,d2
    ;lsr.l   #8,d2               ; d2 = A31-A24 byte

    ; High nibble A31-A28 to $44
    ;move.w  d2,d5
    ;and.w   #$F0,d5             ; Mask high nibble
    ;lsl.w   #8,d5               ; Shift to bits 15-12
    ;move.w  d5,$44(a0)

    ; Low nibble A27-A24 to $46
    ;move.w  d2,d5
    ;and.w   #$0F,d5             ; Mask low nibble
    ;lsl.w   #8,d5
    ;lsl.w   #4,d5               ; Shift to bits 15-12
    ;move.w  d5,$46(a0)

    ; Write A23-A16 (byte $20) as nibbles
    ;move.l  d1,d2
    ;lsr.l   #8,d2
    ;lsr.l   #8,d2               ; d2 = A23-A16 byte

    ; Low nibble A19-A16 to $4A FIRST
    ;move.w  d2,d5
    ;and.w   #$0F,d5             ; Mask low nibble
    ;lsl.w   #8,d5
    ;lsl.w   #4,d5               ; Shift to bits 15-12
    ;move.w  d5,$4A(a0)

    ; High nibble A23-A20 to $48 LAST - TRIGGERS!
    ;move.w  d2,d5
    ;and.w   #$F0,d5             ; Mask high nibble
    ;lsl.w   #8,d5               ; Shift to bits 15-12
    ;move.w  d5,$48(a0)          ; TRIGGERS!

    ; ORANGE - Card relocated
    move.w  #$F80,COLOR00(a1)

    lea     CardRelocatedMsg(pc),a0
    bsr     SerialPutString

    ; Get size code from saved er_Type
    moveq   #0,d1
    move.b  (sp)+,d1            ; Pop er_Type
    and.b   #$07,d1             ; Extract size bits 2-0

    ; Debug: print size code
    lea     SizeCodeMsg(pc),a0
    bsr     SerialPutString
    move.l  d1,d5               ; Use d5 for printing (preserve d0!)
    move.l  d5,d0
    bsr     SerialPutHex
    lea     NewlineMsg(pc),a0
    bsr     SerialPutString

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
    ; Debug: print calculated size
    lea     CalcSizeMsg(pc),a0
    bsr     SerialPutString
    move.l  d2,d5               ; Use d5 for printing (preserve d0!)
    move.l  d5,d0
    bsr     SerialPutHex
    lea     NewlineMsg(pc),a0
    bsr     SerialPutString

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
    move.b  #$FF,$4C(a0)        ; Write to shutup register
    bra   .next_card

.no_card:
    lea     NoCardMsg(pc),a0
    bsr     SerialPutString

.done:
    ; BLUE - Autoconfig done
    move.w  #$00F,COLOR00(a1)

    ; Print completion message
    lea     AutoconfigDoneMsg(pc),a0
    bsr     SerialPutString
    move.l  d0,-(sp)
    bsr     SerialPutHex
    lea     NewlineMsg(pc),a0
    bsr     SerialPutString
    move.l  (sp)+,d0

    movem.l (sp)+,d1-d5/a0-a1
    rts

; ============================================================
; Data
; ============================================================
AutoconfigStartMsg:
    dc.b    "Autoconfig: Scanning Zorro bus...",10,13,0
    even

ReadingCardMsg:
    dc.b    "  Reading $E80000: $",0
    even

NoCardMsg:
    dc.b    "  No card found",10,13,0
    even

MemoryCardMsg:
    dc.b    "  Memory card found!",10,13,0
    even

AllocatingAtMsg:
    dc.b    "  Allocating at: ",0
    even

CardRelocatedMsg:
    dc.b    "  Card relocated",10,13,0
    even

IOCardMsg:
    dc.b    "  I/O card found, shutting up",10,13,0
    even

MaxCardsMsg:
    dc.b    "  Max cards reached",10,13,0
    even

AutoconfigDoneMsg:
    dc.b    "Autoconfig: Done, first RAM base: ",0
    even

SizeCodeMsg:
    dc.b    "  Size code: ",0
    even

CalcSizeMsg:
    dc.b    "  Calculated size: ",0
    even

WritingBaseMsg:
    dc.b    "  Writing base address...",10,13,0
    even
