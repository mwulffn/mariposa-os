; Minimal Amiga bare-metal bootstrap
; Assembles with VASM: vasmm68k_mot -Fbin -o kick.rom bootstrap.s

; ============================================================
; Custom chip registers (active at $DFF000)
; ============================================================
CUSTOM          equ $DFF000

DMACONR         equ $002    ; DMA control read
VPOSR           equ $004    ; Vertical beam position
VHPOSR          equ $006    ; V/H beam position

BPLCON0         equ $100    ; Bitplane control 0
BPLCON1         equ $102    ; Bitplane control 1
BPLCON2         equ $104    ; Bitplane control 2
BPL1PTH         equ $0E0    ; Bitplane 1 pointer high
BPL1PTL         equ $0E2    ; Bitplane 1 pointer low

DIWSTRT         equ $08E    ; Display window start
DIWSTOP         equ $090    ; Display window stop
DDFSTRT         equ $092    ; Data fetch start
DDFSTOP         equ $094    ; Data fetch stop

DMACON          equ $096    ; DMA control write
INTENA          equ $09A    ; Interrupt enable
INTREQ          equ $09C    ; Interrupt request

COLOR00         equ $180    ; Color register 0 (background)
COLOR01         equ $182    ; Color register 1 (foreground)

COP1LCH         equ $080    ; Copper list 1 pointer high
COP1LCL         equ $082    ; Copper list 1 pointer low
COPJMP1         equ $088    ; Copper restart at list 1

; ============================================================
; CIA-A for keyboard (active at $BFE001)
; ============================================================
CIAA            equ $BFE001
CIAA_SDR        equ $C00    ; Serial data register (keyboard)

; ============================================================
; CIA-A for keyboard and overlay control
; ============================================================
CIAA_PRA        equ $BFE001         ; Port A data register
CIAA_DDRA       equ $BFE201         ; Port A data direction


; ============================================================
; Memory layout (chip RAM)
; ============================================================
SCREEN          equ $20000          ; Bitplane at 128KB
COPPERLIST      equ $1F000          ; Copper list
STACK           equ $70FFE         ; Stack at top of 1MB fast RAM ($C00000-$CFFFFF)

; Display constants
SCREEN_WIDTH    equ 320
SCREEN_HEIGHT   equ 256
BYTES_PER_ROW   equ (SCREEN_WIDTH/8)    ; 40 bytes

; ============================================================
; ROM header (simplified - real Kickstart has more structure)
; ============================================================
    org $FC0000             ; Kickstart ROM location (256KB ROM)
    
RomStart:
    dc.l    STACK           ; Initial SSP
    dc.l    Start           ; Initial PC

; ============================================================
; Entry point after reset
; ============================================================
Start:
    ; Disable interrupts at CPU level
    move.w  #$2700,sr
    
    ; Set up stack
    lea     STACK,sp
    
    ; Point to custom chips
    lea     CUSTOM,a6
    
    ; Disable all DMA and interrupts
    move.w  #$7FFF,DMACON(a6)   ; Clear all DMA
    move.w  #$7FFF,INTENA(a6)   ; Clear all interrupts
    move.w  #$7FFF,INTREQ(a6)   ; Clear pending interrupts
    
    ; Disable ROM overlay so chip RAM is visible at $0
    ; OVL is bit 0 of CIA-A PRA - set as output, then clear it
    move.b  #$03,CIAA_DDRA      ; Bits 0,1 as outputs (OVL + LED)
    move.b  #$00,CIAA_PRA       ; Clear OVL bit - ROM overlay off


    ; DEBUG: Immediate red background to confirm CPU is running
    move.w  #$0F00,COLOR00(a6)  ; Bright red - should see this immediately
    
    ; Clear the screen memory
    ; bsr     ClearScreen
    move.w  #$0F0,COLOR00(a6)   ; Green - cleared screen
    
    ; Set up display registers
    move.w  #$1200,BPLCON0(a6)  ; 1 bitplane, color on
    move.w  #$0000,BPLCON1(a6)  ; No horizontal scroll
    move.w  #$0000,BPLCON2(a6)  ; Sprite priority
    
    ; Standard PAL display window
    move.w  #$2C81,DIWSTRT(a6)  ; Display window start
    move.w  #$2CC1,DIWSTOP(a6)  ; Display window stop
    move.w  #$0038,DDFSTRT(a6)  ; Data fetch start
    move.w  #$00D0,DDFSTOP(a6)  ; Data fetch stop
    move.w  #$00F,COLOR00(a6)   ; Blue - display regs set
    
    ; Set bitplane pointer directly (in addition to copper)
    move.l  #SCREEN,d0
    move.w  d0,BPL1PTL(a6)
    swap    d0
    move.w  d0,BPL1PTH(a6)
    move.w  #$FF0,COLOR00(a6)   ; Yellow - bitplane ptr set
    
    ; Set up copper list
    bsr     InitCopper
    move.w  #$F0F,COLOR00(a6)   ; Magenta - copper list built
    
    ; Set colors: dark blue background, white text
    move.w  #$0006,COLOR00(a6)  ; Background: dark blue
    move.w  #$0FFF,COLOR01(a6)  ; Foreground: white
    
    ; Start copper
    move.l  #COPPERLIST,d0
    move.w  d0,COP1LCL(a6)
    swap    d0
    move.w  d0,COP1LCH(a6)
    move.w  COPJMP1(a6),d0      ; Strobe to start copper
    
    ; Enable DMA: bitplane + copper + master
    move.w  #$8380,DMACON(a6)   ; SET + BPLEN + COPEN + DMAEN
    
    ; Print our boot message
    lea     BootMsg(pc),a0
    moveq   #2,d0               ; X position (chars)
    moveq   #2,d1               ; Y position (chars)
    bsr     PrintString
    
    ; Main loop - just wait forever for now
MainLoop:
    bra.s   MainLoop

; ============================================================
; Clear screen memory
; ============================================================
ClearScreen:
    lea     SCREEN,a0
    move.w  #((SCREEN_HEIGHT*BYTES_PER_ROW)/4)-1,d0
.loop:
    clr.l   (a0)+
    dbf     d0,.loop
    rts

; ============================================================
; Initialize copper list
; ============================================================
InitCopper:
    lea     COPPERLIST,a0
    
    ; Set bitplane pointer each frame
    move.w  #BPL1PTH,(a0)+
    move.w  #(SCREEN>>16)&$FFFF,(a0)+
    move.w  #BPL1PTL,(a0)+
    move.w  #SCREEN&$FFFF,(a0)+
    
    ; End of copper list
    move.l  #$FFFFFFFE,(a0)+
    rts

; ============================================================
; Print a character
; d0.w = X position (character column, 0-39)
; d1.w = Y position (character row, 0-31)
; d2.b = character to print
; ============================================================
PrintChar:
    movem.l d0-d4/a0-a2,-(sp)
    
    ; Calculate screen address
    ; addr = SCREEN + (y * 8 * 40) + x
    mulu    #8*BYTES_PER_ROW,d1
    add.w   d0,d1
    lea     SCREEN,a1
    add.l   d1,a1               ; a1 = screen destination
    
    ; Get font data pointer
    ; Assuming ASCII 32-127, font starts at char 32
    sub.b   #32,d2
    ext.w   d2
    lsl.w   #3,d2               ; * 8 bytes per char
    lea     Font(pc),a0
    add.w   d2,a0               ; a0 = font source
    
    ; Copy 8 rows of font data
    moveq   #7,d3
.charloop:
    move.b  (a0)+,(a1)
    add.w   #BYTES_PER_ROW,a1   ; Next screen row
    dbf     d3,.charloop
    
    movem.l (sp)+,d0-d4/a0-a2
    rts

; ============================================================
; Print a null-terminated string
; a0 = pointer to string
; d0.w = X position
; d1.w = Y position
; ============================================================
PrintString:
    movem.l d0-d2/a0,-(sp)
    move.w  d0,d3               ; Save X
.loop:
    move.b  (a0)+,d2
    beq.s   .done
    bsr     PrintChar
    addq.w  #1,d0               ; Next column
    bra.s   .loop
.done:
    movem.l (sp)+,d0-d2/a0
    rts

; ============================================================
; Boot message
; ============================================================
BootMsg:
    dc.b    "READY.",0
    even

; ============================================================
; Simple 8x8 font (space through ~, ASCII 32-126)
; Just showing a few essential characters to start
; ============================================================
Font:
; Space (32)
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00000000

; ! (33)
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00000000
    dc.b    %00011000
    dc.b    %00000000

; " through , (34-44) - abbreviated, fill these in
    dcb.b   11*8,0

; - (45)
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %01111110
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00000000

; . (46)
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00000000

; / through @ (47-64) - abbreviated
    dcb.b   18*8,0

; A (65)
    dc.b    %00111100
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01111110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %00000000

; B (66)
    dc.b    %01111100
    dc.b    %01100110
    dc.b    %01111100
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01111100
    dc.b    %00000000

; C-C skipped, add D (68)
    dcb.b   1*8,0

; D (68)
    dc.b    %01111100
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01111100
    dc.b    %00000000

; E (69)
    dc.b    %01111110
    dc.b    %01100000
    dc.b    %01111100
    dc.b    %01100000
    dc.b    %01100000
    dc.b    %01100000
    dc.b    %01111110
    dc.b    %00000000

; F-Q abbreviated
    dcb.b   12*8,0

; R (82)
    dc.b    %01111100
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01111100
    dc.b    %01101100
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %00000000

; S-X abbreviated  
    dcb.b   6*8,0

; Y (89)
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %00111100
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00000000

; Fill rest of font table to get proper indexing
; In a real implementation, complete the full ASCII set
    dcb.b   38*8,0

; ============================================================
; Pad ROM to expected size and add footer
; ============================================================
    org $FFFFFC
RomEnd:
    dc.l    RomStart            ; ROM checksum location (simplified)

    end
