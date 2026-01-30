; ============================================================
; bootstrap.s - Main ROM entry point
; ============================================================
; Assembles with VASM: vasmm68k_mot -Fbin -I src -o kick.rom bootstrap.s
; ============================================================

    include "hardware.i"

; Panic and PanicFont are defined in debug.s which is included at the end

; ============================================================
; ROM header
; ============================================================
    org $FC0000

RomStart:
    dc.l    STACK               ; Initial SSP
    dc.l    Start               ; Initial PC

; ============================================================
; Entry point
; ============================================================
Start:
    ; Disable interrupts at CPU level
    move.w  #$2700,sr
    
    ; Set up stack
    lea     STACK,sp
    
    ; Point to custom chips
    lea     CUSTOM,a6
    
    ; Disable all DMA and interrupts
    move.w  #$7FFF,DMACON(a6)
    move.w  #$7FFF,INTENA(a6)
    move.w  #$7FFF,INTREQ(a6)
    
    ; Disable ROM overlay so chip RAM is visible at $0
    move.b  #$03,CIAA_DDRA
    move.b  #$00,CIAA_PRA
    
    ; Clear screen memory
    lea     SCREEN,a0
    move.w  #((SCREEN_HEIGHT*BYTES_PER_ROW)/4)-1,d0
.clrloop:
    clr.l   (a0)+
    dbf     d0,.clrloop
    
    ; Set up display
    move.w  #$1200,BPLCON0(a6)      ; 1 bitplane, color on
    move.w  #$0000,BPLCON1(a6)
    move.w  #$0000,BPLCON2(a6)
    
    ; PAL display window
    move.w  #$2C81,DIWSTRT(a6)
    move.w  #$2CC1,DIWSTOP(a6)
    move.w  #$0038,DDFSTRT(a6)
    move.w  #$00D0,DDFSTOP(a6)
    
    ; Set bitplane pointer
    move.l  #SCREEN,d0
    move.w  d0,BPL1PTL(a6)
    swap    d0
    move.w  d0,BPL1PTH(a6)
    
    ; Set up copper list
    bsr     InitCopper
    
    ; Set colors
    move.w  #$0006,COLOR00(a6)      ; Dark blue background
    move.w  #$0FFF,COLOR01(a6)      ; White text
    
    ; Start copper
    move.l  #COPPERLIST,d0
    move.w  d0,COP1LCL(a6)
    swap    d0
    move.w  d0,COP1LCH(a6)
    move.w  COPJMP1(a6),d0
    
    ; Enable DMA
    move.w  #$8380,DMACON(a6)       ; SET + BPLEN + COPEN + DMAEN
    
    ; Print boot message
    lea     BootMsg(pc),a0
    moveq   #2,d0
    moveq   #2,d1
    bsr     PrintString
    
    ; Test: Call debug after 'READY.'
    ; Remove this once we're past testing
    jsr Panic
    
    ; Main loop
MainLoop:
    bra.s   MainLoop

; ============================================================
; InitCopper - Build copper list
; ============================================================
InitCopper:
    lea     COPPERLIST,a0
    
    move.w  #BPL1PTH,(a0)+
    move.w  #(SCREEN>>16)&$FFFF,(a0)+
    move.w  #BPL1PTL,(a0)+
    move.w  #SCREEN&$FFFF,(a0)+
    
    move.l  #$FFFFFFFE,(a0)+        ; End
    rts

; ============================================================
; PrintString - Print null-terminated string
; a0 = string, d0.w = X, d1.w = Y
; ============================================================
PrintString:
    movem.l d0-d3/a0,-(sp)
    move.w  d0,d3
.loop:
    move.b  (a0)+,d2
    beq.s   .done
    bsr     PrintChar
    addq.w  #1,d0
    bra.s   .loop
.done:
    movem.l (sp)+,d0-d3/a0
    rts

; ============================================================
; PrintChar - Render character to screen
; d0.w = X, d1.w = Y, d2.b = char
; ============================================================
PrintChar:
    movem.l d0-d4/a0-a1,-(sp)
    
    ; Bounds check
    cmp.b   #32,d2
    blt.s   .done
    cmp.b   #127,d2
    bge.s   .done
    
    ; Screen address
    mulu    #8*BYTES_PER_ROW,d1
    add.w   d0,d1
    lea     SCREEN,a1
    add.l   d1,a1
    
    ; Font pointer (use PanicFont from debug.s)
    sub.b   #32,d2
    ext.w   d2
    lsl.w   #3,d2
    lea     PanicFont,a0
    add.w   d2,a0
    
    ; Copy 8 rows
    moveq   #7,d4
.charloop:
    move.b  (a0)+,(a1)
    add.w   #BYTES_PER_ROW,a1
    dbf     d4,.charloop
    
.done:
    movem.l (sp)+,d0-d4/a0-a1
    rts

; ============================================================
; Data
; ============================================================
BootMsg:
    dc.b    "READY.",0
    even

; ============================================================
; Include debug module
; ============================================================
    include "debug.s"

; ============================================================
; ROM footer - pad to 256KB and add checksum location
; ============================================================
    org $FFFFFC
RomEnd:
    dc.l    RomStart

    end
