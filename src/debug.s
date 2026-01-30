; ============================================================
; debug.s - Debug/crash handler for ROM
; ============================================================
; 
; Provides Debug() function that:
;   1. Saves all registers
;   2. Sets up known display state
;   3. Prints CPU state
;   4. Halts
;
; Call with:  JSR Debug
; Or install as exception handler
; ============================================================

; hardware.i already included by bootstrap.s

; ============================================================
; Constants
; ============================================================
DEBUG_BG_COLOR      equ $0008       ; Dark blue background
DEBUG_FG_COLOR      equ $0FFF       ; White text
DEBUG_ERR_COLOR     equ $0F00       ; Red for error title

; Screen layout
DEBUG_TITLE_Y       equ 2
DEBUG_REGS_Y        equ 5
DEBUG_MSG_Y         equ 20

; ============================================================
; Register save area (fixed location in chip RAM)
; ============================================================
SAVE_AREA       equ $1000           ; Safe area below screen

SavedRegs       equ SAVE_AREA
SavedD0         equ SAVE_AREA+$00
SavedD1         equ SAVE_AREA+$04
SavedD2         equ SAVE_AREA+$08
SavedD3         equ SAVE_AREA+$0C
SavedD4         equ SAVE_AREA+$10
SavedD5         equ SAVE_AREA+$14
SavedD6         equ SAVE_AREA+$18
SavedD7         equ SAVE_AREA+$1C
SavedA0         equ SAVE_AREA+$20
SavedA1         equ SAVE_AREA+$24
SavedA2         equ SAVE_AREA+$28
SavedA3         equ SAVE_AREA+$2C
SavedA4         equ SAVE_AREA+$30
SavedA5         equ SAVE_AREA+$34
SavedA6         equ SAVE_AREA+$38
SavedA7         equ SAVE_AREA+$3C
SavedSR         equ SAVE_AREA+$40
SavedPC         equ SAVE_AREA+$44
PanicMsgPtr    equ SAVE_AREA+$48

; ============================================================
; Debug - Main entry point
; ============================================================
; Call this to dump state and halt.
; All registers are preserved for display.
; ============================================================
Panic:
    ; Save registers immediately before we trash anything
    movem.l d0-d7/a0-a6,SavedRegs
    
    ; Save A7 (stack pointer) - it's not in movem range
    move.l  sp,SavedA7
    
    ; Save SR
    move.w  sr,SavedSR
    
    ; Save return address as PC (caller's location)
    move.l  (sp),SavedPC
    
    ; Now we can use registers freely
    bsr PanicInitDisplay
    bsr PanicPrintTitle
    bsr PanicPrintRegs
    bsr PanicPrintMsg

    ; Output to serial port
    bsr PanicSerialOutput

    ; Halt
.halt:
    bra.s   .halt

; ============================================================
; PanicWithMsg - Entry point preserving a message
; ============================================================
; A0 = pointer to message string (null terminated)
; Saves message pointer, then calls Debug
; ============================================================
PanicWithMsg:
    move.l  a0,PanicMsgPtr
    bra.s   Panic

; ============================================================
; PanicInitCopper - Build minimal copper list for Panic
; ============================================================
PanicInitCopper:
    lea     COPPERLIST,a0

    ; Set BPL1PTH register (high word of screen address)
    move.w  #BPL1PTH,(a0)+
    move.w  #(SCREEN>>16)&$FFFF,(a0)+

    ; Set BPL1PTL register (low word of screen address)
    move.w  #BPL1PTL,(a0)+
    move.w  #SCREEN&$FFFF,(a0)+

    ; End copper list
    move.l  #$FFFFFFFE,(a0)+

    rts

; ============================================================
; PanicInitDisplay - Set up minimal known display state
; ============================================================
; Matches bootstrap.s sequence with copper list added
; ============================================================
PanicInitDisplay:
    lea     CUSTOM,a6

    ; Disable all DMA and interrupts
    move.w  #$7FFF,DMACON(a6)
    move.w  #$7FFF,INTENA(a6)
    move.w  #$7FFF,INTREQ(a6)

    ; Clear screen memory FIRST
    lea     SCREEN,a0
    move.w  #((256*BYTES_PER_ROW)/4)-1,d0
.clr:
    clr.l   (a0)+
    dbf     d0,.clr

    ; Set up display registers
    move.w  #$1200,BPLCON0(a6)      ; 1 bitplane, color on
    move.w  #$0000,BPLCON1(a6)
    move.w  #$0000,BPLCON2(a6)
    move.w  #$0000,BPL1MOD(a6)
    move.w  #$0000,BPL2MOD(a6)

    ; PAL display window
    move.w  #$2C81,DIWSTRT(a6)
    move.w  #$2CC1,DIWSTOP(a6)
    move.w  #$0038,DDFSTRT(a6)
    move.w  #$00D0,DDFSTOP(a6)

    ; Set bitplane pointer (initial)
    move.l  #SCREEN,d0
    move.w  d0,BPL1PTL(a6)
    swap    d0
    move.w  d0,BPL1PTH(a6)

    ; Build copper list
    bsr     PanicInitCopper

    ; Set colors
    move.w  #DEBUG_BG_COLOR,COLOR00(a6)
    move.w  #DEBUG_FG_COLOR,COLOR01(a6)

    ; Start copper
    move.l  #COPPERLIST,d0
    move.w  d0,COP1LCL(a6)
    swap    d0
    move.w  d0,COP1LCH(a6)
    move.w  COPJMP1(a6),d0          ; Trigger copper start

    ; Enable DMA with copper
    move.w  #$8380,DMACON(a6)       ; SET + BPLEN + COPEN + DMAEN

    rts

; ============================================================
; PanicPrintTitle - Print header
; ============================================================
PanicPrintTitle:
    ; Don't change colors - single bitplane means only one foreground color
    ; Changing COLOR01 while display is active causes flickering

    lea     .title(pc),a0
    moveq   #2,d0                   ; X
    moveq   #DEBUG_TITLE_Y,d1       ; Y
    bsr PanicPrintStr

    ; Separator line
    lea     .separator(pc),a0
    moveq   #2,d0
    moveq   #DEBUG_TITLE_Y+1,d1
    bsr PanicPrintStr

    rts

.title:
    dc.b    "=== SYSTEM DEBUG ===",0
.separator:
    dc.b    "--------------------",0
    even

; ============================================================
; PanicPrintRegs - Print all saved registers
; ============================================================
PanicPrintRegs:
    ; Data registers
    moveq   #DEBUG_REGS_Y,d4        ; Current Y position
    
    ; D0-D3 on one line
    lea     .lblD0(pc),a0
    moveq   #2,d0
    move.w  d4,d1
    bsr PanicPrintStr
    
    move.l  SavedD0,d3
    moveq   #6,d0
    move.w  d4,d1
    bsr PanicPrintHex32
    
    lea     .lblD1(pc),a0
    moveq   #17,d0
    move.w  d4,d1
    bsr PanicPrintStr
    
    move.l  SavedD1,d3
    moveq   #21,d0
    move.w  d4,d1
    bsr PanicPrintHex32
    
    addq.w  #1,d4                   ; Next line
    
    ; D2-D3
    lea     .lblD2(pc),a0
    moveq   #2,d0
    move.w  d4,d1
    bsr PanicPrintStr
    
    move.l  SavedD2,d3
    moveq   #6,d0
    move.w  d4,d1
    bsr PanicPrintHex32
    
    lea     .lblD3(pc),a0
    moveq   #17,d0
    move.w  d4,d1
    bsr PanicPrintStr
    
    move.l  SavedD3,d3
    moveq   #21,d0
    move.w  d4,d1
    bsr PanicPrintHex32
    
    addq.w  #1,d4                   ; Next line
    
    ; D4-D5
    lea     .lblD4(pc),a0
    moveq   #2,d0
    move.w  d4,d1
    bsr PanicPrintStr
    
    move.l  SavedD4,d3
    moveq   #6,d0
    move.w  d4,d1
    bsr PanicPrintHex32
    
    lea     .lblD5(pc),a0
    moveq   #17,d0
    move.w  d4,d1
    bsr PanicPrintStr
    
    move.l  SavedD5,d3
    moveq   #21,d0
    move.w  d4,d1
    bsr PanicPrintHex32
    
    addq.w  #1,d4                   ; Next line
    
    ; D6-D7
    lea     .lblD6(pc),a0
    moveq   #2,d0
    move.w  d4,d1
    bsr PanicPrintStr
    
    move.l  SavedD6,d3
    moveq   #6,d0
    move.w  d4,d1
    bsr PanicPrintHex32
    
    lea     .lblD7(pc),a0
    moveq   #17,d0
    move.w  d4,d1
    bsr PanicPrintStr
    
    move.l  SavedD7,d3
    moveq   #21,d0
    move.w  d4,d1
    bsr PanicPrintHex32
    
    addq.w  #2,d4                   ; Skip line
    
    ; Address registers A0-A1
    lea     .lblA0(pc),a0
    moveq   #2,d0
    move.w  d4,d1
    bsr PanicPrintStr
    
    move.l  SavedA0,d3
    moveq   #6,d0
    move.w  d4,d1
    bsr PanicPrintHex32
    
    lea     .lblA1(pc),a0
    moveq   #17,d0
    move.w  d4,d1
    bsr PanicPrintStr
    
    move.l  SavedA1,d3
    moveq   #21,d0
    move.w  d4,d1
    bsr PanicPrintHex32
    
    addq.w  #1,d4
    
    ; A2-A3
    lea     .lblA2(pc),a0
    moveq   #2,d0
    move.w  d4,d1
    bsr PanicPrintStr
    
    move.l  SavedA2,d3
    moveq   #6,d0
    move.w  d4,d1
    bsr PanicPrintHex32
    
    lea     .lblA3(pc),a0
    moveq   #17,d0
    move.w  d4,d1
    bsr PanicPrintStr
    
    move.l  SavedA3,d3
    moveq   #21,d0
    move.w  d4,d1
    bsr PanicPrintHex32
    
    addq.w  #1,d4
    
    ; A4-A5
    lea     .lblA4(pc),a0
    moveq   #2,d0
    move.w  d4,d1
    bsr PanicPrintStr
    
    move.l  SavedA4,d3
    moveq   #6,d0
    move.w  d4,d1
    bsr PanicPrintHex32
    
    lea     .lblA5(pc),a0
    moveq   #17,d0
    move.w  d4,d1
    bsr PanicPrintStr
    
    move.l  SavedA5,d3
    moveq   #21,d0
    move.w  d4,d1
    bsr PanicPrintHex32
    
    addq.w  #1,d4
    
    ; A6-A7
    lea     .lblA6(pc),a0
    moveq   #2,d0
    move.w  d4,d1
    bsr PanicPrintStr
    
    move.l  SavedA6,d3
    moveq   #6,d0
    move.w  d4,d1
    bsr PanicPrintHex32
    
    lea     .lblA7(pc),a0
    moveq   #17,d0
    move.w  d4,d1
    bsr PanicPrintStr
    
    move.l  SavedA7,d3
    moveq   #21,d0
    move.w  d4,d1
    bsr PanicPrintHex32
    
    addq.w  #2,d4                   ; Skip line
    
    ; PC and SR
    lea     .lblPC(pc),a0
    moveq   #2,d0
    move.w  d4,d1
    bsr PanicPrintStr
    
    move.l  SavedPC,d3
    moveq   #6,d0
    move.w  d4,d1
    bsr PanicPrintHex32
    
    lea     .lblSR(pc),a0
    moveq   #17,d0
    move.w  d4,d1
    bsr PanicPrintStr
    
    move.w  SavedSR,d3
    moveq   #21,d0
    move.w  d4,d1
    bsr PanicPrintHex16
    
    rts

.lblD0: dc.b "D0:",0
.lblD1: dc.b "D1:",0
.lblD2: dc.b "D2:",0
.lblD3: dc.b "D3:",0
.lblD4: dc.b "D4:",0
.lblD5: dc.b "D5:",0
.lblD6: dc.b "D6:",0
.lblD7: dc.b "D7:",0
.lblA0: dc.b "A0:",0
.lblA1: dc.b "A1:",0
.lblA2: dc.b "A2:",0
.lblA3: dc.b "A3:",0
.lblA4: dc.b "A4:",0
.lblA5: dc.b "A5:",0
.lblA6: dc.b "A6:",0
.lblA7: dc.b "A7:",0
.lblPC: dc.b "PC:",0
.lblSR: dc.b "SR:",0
    even

; ============================================================
; PanicPrintMsg - Print user message if set
; ============================================================
PanicPrintMsg:
    move.l  PanicMsgPtr,d0
    beq.s   .nomsg
    
    move.l  d0,a0
    moveq   #2,d0
    moveq   #DEBUG_MSG_Y,d1
    bsr PanicPrintStr
    
.nomsg:
    rts

; ============================================================
; PanicSerialOutput - Send register dump to serial port
; ============================================================
PanicSerialOutput:
    movem.l d0-d7/a0-a6,-(sp)

    ; Send header
    lea     .header(pc),a0
    bsr     SerialPutString

    ; Send all data registers (D0-D7)
    lea     .lblD0(pc),a0
    bsr     SerialPutString
    move.l  SavedD0,d0
    bsr     PanicSerialHex32

    lea     .lblD1(pc),a0
    bsr     SerialPutString
    move.l  SavedD1,d0
    bsr     PanicSerialHex32

    lea     .lblD2(pc),a0
    bsr     SerialPutString
    move.l  SavedD2,d0
    bsr     PanicSerialHex32

    lea     .lblD3(pc),a0
    bsr     SerialPutString
    move.l  SavedD3,d0
    bsr     PanicSerialHex32
    bsr     .crlf

    lea     .lblD4(pc),a0
    bsr     SerialPutString
    move.l  SavedD4,d0
    bsr     PanicSerialHex32

    lea     .lblD5(pc),a0
    bsr     SerialPutString
    move.l  SavedD5,d0
    bsr     PanicSerialHex32

    lea     .lblD6(pc),a0
    bsr     SerialPutString
    move.l  SavedD6,d0
    bsr     PanicSerialHex32

    lea     .lblD7(pc),a0
    bsr     SerialPutString
    move.l  SavedD7,d0
    bsr     PanicSerialHex32
    bsr     .crlf

    ; Send all address registers (A0-A7)
    lea     .lblA0(pc),a0
    bsr     SerialPutString
    move.l  SavedA0,d0
    bsr     PanicSerialHex32

    lea     .lblA1(pc),a0
    bsr     SerialPutString
    move.l  SavedA1,d0
    bsr     PanicSerialHex32

    lea     .lblA2(pc),a0
    bsr     SerialPutString
    move.l  SavedA2,d0
    bsr     PanicSerialHex32

    lea     .lblA3(pc),a0
    bsr     SerialPutString
    move.l  SavedA3,d0
    bsr     PanicSerialHex32
    bsr     .crlf

    lea     .lblA4(pc),a0
    bsr     SerialPutString
    move.l  SavedA4,d0
    bsr     PanicSerialHex32

    lea     .lblA5(pc),a0
    bsr     SerialPutString
    move.l  SavedA5,d0
    bsr     PanicSerialHex32

    lea     .lblA6(pc),a0
    bsr     SerialPutString
    move.l  SavedA6,d0
    bsr     PanicSerialHex32

    lea     .lblA7(pc),a0
    bsr     SerialPutString
    move.l  SavedA7,d0
    bsr     PanicSerialHex32
    bsr     .crlf

    ; Send PC and SR
    lea     .lblPC(pc),a0
    bsr     SerialPutString
    move.l  SavedPC,d0
    bsr     PanicSerialHex32

    lea     .lblSR(pc),a0
    bsr     SerialPutString
    move.w  SavedSR,d0
    bsr     PanicSerialHex16
    bsr     .crlf

    ; Send custom message if present
    move.l  PanicMsgPtr,d0
    beq.s   .done
    bsr     .crlf
    move.l  d0,a0
    bsr     SerialPutString
    bsr     .crlf

.done:
    movem.l (sp)+,d0-d7/a0-a6
    rts

.crlf:
    move.b  #10,d0
    bsr     SerialPutChar
    move.b  #13,d0
    bsr     SerialPutChar
    rts

.header:
    dc.b    10,13,"=== SYSTEM DEBUG ===",10,13,0
.lblD0: dc.b "D0:$",0
.lblD1: dc.b " D1:$",0
.lblD2: dc.b " D2:$",0
.lblD3: dc.b " D3:$",0
.lblD4: dc.b "D4:$",0
.lblD5: dc.b " D5:$",0
.lblD6: dc.b " D6:$",0
.lblD7: dc.b " D7:$",0
.lblA0: dc.b "A0:$",0
.lblA1: dc.b " A1:$",0
.lblA2: dc.b " A2:$",0
.lblA3: dc.b " A3:$",0
.lblA4: dc.b "A4:$",0
.lblA5: dc.b " A5:$",0
.lblA6: dc.b " A6:$",0
.lblA7: dc.b " A7:$",0
.lblPC: dc.b "PC:$",0
.lblSR: dc.b " SR:$",0
    even

; ============================================================
; PanicSerialHex32 - Send 32-bit hex value to serial
; ============================================================
; d0.l = value to send
PanicSerialHex32:
    movem.l d0-d3,-(sp)
    move.l  d0,d3                   ; Save original value
    moveq   #7,d2                   ; 8 nibbles
.loop:
    rol.l   #4,d3                   ; Rotate next nibble to low position
    move.l  d3,d1
    and.l   #$0F,d1
    cmp.b   #10,d1
    blt.s   .digit
    add.b   #'A'-10,d1
    bra.s   .send
.digit:
    add.b   #'0',d1
.send:
    move.b  d1,d0                   ; Character to send
    bsr     SerialPutChar
    dbf     d2,.loop
    movem.l (sp)+,d0-d3
    rts

; ============================================================
; PanicSerialHex16 - Send 16-bit hex value to serial
; ============================================================
; d0.w = value to send
PanicSerialHex16:
    movem.l d0-d3,-(sp)
    move.w  d0,d3                   ; Save original value
    moveq   #3,d2                   ; 4 nibbles
.loop:
    rol.w   #4,d3
    move.w  d3,d1
    and.w   #$0F,d1
    cmp.b   #10,d1
    blt.s   .digit
    add.b   #'A'-10,d1
    bra.s   .send
.digit:
    add.b   #'0',d1
.send:
    move.b  d1,d0
    bsr     SerialPutChar
    dbf     d2,.loop
    movem.l (sp)+,d0-d3
    rts

; ============================================================
; PanicPrintStr - Print null-terminated string
; ============================================================
; a0 = string pointer
; d0.w = X position (chars)
; d1.w = Y position (chars)
; ============================================================
PanicPrintStr:
    movem.l d0-d3/a0-a2,-(sp)
    move.w  d0,d2                   ; Save X
.loop:
    move.b  (a0)+,d3
    beq.s   .done
    move.w  d2,d0
    bsr PanicPrintChar
    addq.w  #1,d2
    bra.s   .loop
.done:
    movem.l (sp)+,d0-d3/a0-a2
    rts

; ============================================================
; PanicPrintHex32 - Print 32-bit hex value
; ============================================================
; d3.l = value to print
; d0.w = X position
; d1.w = Y position
; ============================================================
PanicPrintHex32:
    movem.l d0-d5,-(sp)
    move.w  d0,d4                   ; Save X
    move.l  d3,d5                   ; Save value
    
    ; Print $ prefix
    move.b  #'$',d3
    bsr PanicPrintChar
    addq.w  #1,d4
    
    ; Print 8 hex digits
    moveq   #7,d2                   ; Counter
.loop:
    rol.l   #4,d5                   ; Rotate next nibble into low position FIRST
    move.l  d5,d3
    and.l   #$0F,d3
    cmp.b   #10,d3
    blt.s   .digit
    add.b   #'A'-10,d3
    bra.s   .print
.digit:
    add.b   #'0',d3
.print:
    move.w  d4,d0
    bsr PanicPrintChar
    addq.w  #1,d4
    dbf     d2,.loop
    
    movem.l (sp)+,d0-d5
    rts

; ============================================================
; PanicPrintHex16 - Print 16-bit hex value
; ============================================================
; d3.w = value to print
; d0.w = X position
; d1.w = Y position
; ============================================================
PanicPrintHex16:
    movem.l d0-d5,-(sp)
    move.w  d0,d4                   ; Save X
    move.w  d3,d5                   ; Save value
    
    ; Print $ prefix
    move.b  #'$',d3
    bsr PanicPrintChar
    addq.w  #1,d4
    
    ; Print 4 hex digits
    moveq   #3,d2                   ; Counter
.loop:
    rol.w   #4,d5                   ; Rotate FIRST
    move.w  d5,d3
    and.w   #$0F,d3
    cmp.b   #10,d3
    blt.s   .digit
    add.b   #'A'-10,d3
    bra.s   .print
.digit:
    add.b   #'0',d3
.print:
    move.w  d4,d0
    bsr PanicPrintChar
    addq.w  #1,d4
    dbf     d2,.loop
    
    movem.l (sp)+,d0-d5
    rts

; ============================================================
; PanicPrintChar - Render single character to screen
; ============================================================
; d0.w = X position (char column)
; d1.w = Y position (char row)
; d3.b = character
; ============================================================
PanicPrintChar:
    movem.l d0-d4/a0-a1,-(sp)
    
    ; Bounds check
    cmp.b   #32,d3
    blt.s   .done
    cmp.b   #127,d3
    bge.s   .done
    
    ; Calculate screen address
    ; addr = SCREEN + (y * 8 * BYTES_PER_ROW) + x
    mulu    #8*BYTES_PER_ROW,d1
    add.w   d0,d1
    lea     SCREEN,a1
    add.l   d1,a1
    
    ; Get font pointer
    sub.b   #32,d3
    ext.w   d3
    lsl.w   #3,d3                   ; * 8 bytes per char
    lea     PanicFont(pc),a0
    add.w   d3,a0
    
    ; Copy 8 rows
    moveq   #7,d4
.charloop:
    move.b  (a0)+,(a1)
    add.w   #BYTES_PER_ROW,a1
    dbf     d4,.charloop
    
.done:
    movem.l (sp)+,d0-d4/a0-a1
    rts

; PanicMsgPtr is now at fixed address (SAVE_AREA+$48)

; ============================================================
; 8x8 Font - ASCII 32-126
; ============================================================

PanicFont:
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
; " (34)
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %00100100
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00000000
; # (35)
    dc.b    %00100100
    dc.b    %00100100
    dc.b    %01111110
    dc.b    %00100100
    dc.b    %01111110
    dc.b    %00100100
    dc.b    %00100100
    dc.b    %00000000
; $ (36)
    dc.b    %00011000
    dc.b    %00111110
    dc.b    %01100000
    dc.b    %00111100
    dc.b    %00000110
    dc.b    %01111100
    dc.b    %00011000
    dc.b    %00000000
; % (37)
    dc.b    %01100010
    dc.b    %01100100
    dc.b    %00001000
    dc.b    %00010000
    dc.b    %00100000
    dc.b    %01001100
    dc.b    %10001100
    dc.b    %00000000
; & (38)
    dc.b    %00110000
    dc.b    %01001000
    dc.b    %00110000
    dc.b    %01010110
    dc.b    %10001000
    dc.b    %10001000
    dc.b    %01110110
    dc.b    %00000000
; ' (39)
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00110000
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00000000
; ( (40)
    dc.b    %00001100
    dc.b    %00011000
    dc.b    %00110000
    dc.b    %00110000
    dc.b    %00110000
    dc.b    %00011000
    dc.b    %00001100
    dc.b    %00000000
; ) (41)
    dc.b    %00110000
    dc.b    %00011000
    dc.b    %00001100
    dc.b    %00001100
    dc.b    %00001100
    dc.b    %00011000
    dc.b    %00110000
    dc.b    %00000000
; * (42)
    dc.b    %00000000
    dc.b    %00100100
    dc.b    %00011000
    dc.b    %01111110
    dc.b    %00011000
    dc.b    %00100100
    dc.b    %00000000
    dc.b    %00000000
; + (43)
    dc.b    %00000000
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %01111110
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00000000
    dc.b    %00000000
; , (44)
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00110000
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
; / (47)
    dc.b    %00000010
    dc.b    %00000100
    dc.b    %00001000
    dc.b    %00010000
    dc.b    %00100000
    dc.b    %01000000
    dc.b    %10000000
    dc.b    %00000000
; 0 (48)
    dc.b    %00111100
    dc.b    %01100110
    dc.b    %01101110
    dc.b    %01110110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %00111100
    dc.b    %00000000
; 1 (49)
    dc.b    %00011000
    dc.b    %00111000
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %01111110
    dc.b    %00000000
; 2 (50)
    dc.b    %00111100
    dc.b    %01100110
    dc.b    %00000110
    dc.b    %00011100
    dc.b    %00110000
    dc.b    %01100000
    dc.b    %01111110
    dc.b    %00000000
; 3 (51)
    dc.b    %00111100
    dc.b    %01100110
    dc.b    %00000110
    dc.b    %00011100
    dc.b    %00000110
    dc.b    %01100110
    dc.b    %00111100
    dc.b    %00000000
; 4 (52)
    dc.b    %00001100
    dc.b    %00011100
    dc.b    %00101100
    dc.b    %01001100
    dc.b    %01111110
    dc.b    %00001100
    dc.b    %00001100
    dc.b    %00000000
; 5 (53)
    dc.b    %01111110
    dc.b    %01100000
    dc.b    %01111100
    dc.b    %00000110
    dc.b    %00000110
    dc.b    %01100110
    dc.b    %00111100
    dc.b    %00000000
; 6 (54)
    dc.b    %00011100
    dc.b    %00110000
    dc.b    %01100000
    dc.b    %01111100
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %00111100
    dc.b    %00000000
; 7 (55)
    dc.b    %01111110
    dc.b    %00000110
    dc.b    %00001100
    dc.b    %00011000
    dc.b    %00110000
    dc.b    %00110000
    dc.b    %00110000
    dc.b    %00000000
; 8 (56)
    dc.b    %00111100
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %00111100
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %00111100
    dc.b    %00000000
; 9 (57)
    dc.b    %00111100
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %00111110
    dc.b    %00000110
    dc.b    %00001100
    dc.b    %00111000
    dc.b    %00000000
; : (58)
    dc.b    %00000000
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00000000
; ; (59)
    dc.b    %00000000
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00110000
; < (60)
    dc.b    %00000110
    dc.b    %00001100
    dc.b    %00011000
    dc.b    %00110000
    dc.b    %00011000
    dc.b    %00001100
    dc.b    %00000110
    dc.b    %00000000
; = (61)
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %01111110
    dc.b    %00000000
    dc.b    %01111110
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00000000
; > (62)
    dc.b    %01100000
    dc.b    %00110000
    dc.b    %00011000
    dc.b    %00001100
    dc.b    %00011000
    dc.b    %00110000
    dc.b    %01100000
    dc.b    %00000000
; ? (63)
    dc.b    %00111100
    dc.b    %01100110
    dc.b    %00000110
    dc.b    %00001100
    dc.b    %00011000
    dc.b    %00000000
    dc.b    %00011000
    dc.b    %00000000
; @ (64)
    dc.b    %00111100
    dc.b    %01100110
    dc.b    %01101110
    dc.b    %01101010
    dc.b    %01101110
    dc.b    %01100000
    dc.b    %00111100
    dc.b    %00000000
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
    dc.b    %01100110
    dc.b    %01111100
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01111100
    dc.b    %00000000
; C (67)
    dc.b    %00111100
    dc.b    %01100110
    dc.b    %01100000
    dc.b    %01100000
    dc.b    %01100000
    dc.b    %01100110
    dc.b    %00111100
    dc.b    %00000000
; D (68)
    dc.b    %01111000
    dc.b    %01101100
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01101100
    dc.b    %01111000
    dc.b    %00000000
; E (69)
    dc.b    %01111110
    dc.b    %01100000
    dc.b    %01100000
    dc.b    %01111100
    dc.b    %01100000
    dc.b    %01100000
    dc.b    %01111110
    dc.b    %00000000
; F (70)
    dc.b    %01111110
    dc.b    %01100000
    dc.b    %01100000
    dc.b    %01111100
    dc.b    %01100000
    dc.b    %01100000
    dc.b    %01100000
    dc.b    %00000000
; G (71)
    dc.b    %00111100
    dc.b    %01100110
    dc.b    %01100000
    dc.b    %01101110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %00111110
    dc.b    %00000000
; H (72)
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01111110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %00000000
; I (73)
    dc.b    %01111110
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %01111110
    dc.b    %00000000
; J (74)
    dc.b    %00111110
    dc.b    %00001100
    dc.b    %00001100
    dc.b    %00001100
    dc.b    %00001100
    dc.b    %01101100
    dc.b    %00111000
    dc.b    %00000000
; K (75)
    dc.b    %01100110
    dc.b    %01101100
    dc.b    %01111000
    dc.b    %01110000
    dc.b    %01111000
    dc.b    %01101100
    dc.b    %01100110
    dc.b    %00000000
; L (76)
    dc.b    %01100000
    dc.b    %01100000
    dc.b    %01100000
    dc.b    %01100000
    dc.b    %01100000
    dc.b    %01100000
    dc.b    %01111110
    dc.b    %00000000
; M (77)
    dc.b    %01100011
    dc.b    %01110111
    dc.b    %01111111
    dc.b    %01101011
    dc.b    %01100011
    dc.b    %01100011
    dc.b    %01100011
    dc.b    %00000000
; N (78)
    dc.b    %01100110
    dc.b    %01110110
    dc.b    %01111110
    dc.b    %01111110
    dc.b    %01101110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %00000000
; O (79)
    dc.b    %00111100
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %00111100
    dc.b    %00000000
; P (80)
    dc.b    %01111100
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01111100
    dc.b    %01100000
    dc.b    %01100000
    dc.b    %01100000
    dc.b    %00000000
; Q (81)
    dc.b    %00111100
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01101010
    dc.b    %01101100
    dc.b    %00110110
    dc.b    %00000000
; R (82)
    dc.b    %01111100
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01111100
    dc.b    %01101100
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %00000000
; S (83)
    dc.b    %00111100
    dc.b    %01100110
    dc.b    %01100000
    dc.b    %00111100
    dc.b    %00000110
    dc.b    %01100110
    dc.b    %00111100
    dc.b    %00000000
; T (84)
    dc.b    %01111110
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00000000
; U (85)
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %00111100
    dc.b    %00000000
; V (86)
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %00111100
    dc.b    %00011000
    dc.b    %00000000
; W (87)
    dc.b    %01100011
    dc.b    %01100011
    dc.b    %01100011
    dc.b    %01101011
    dc.b    %01111111
    dc.b    %01110111
    dc.b    %01100011
    dc.b    %00000000
; X (88)
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %00111100
    dc.b    %00011000
    dc.b    %00111100
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %00000000
; Y (89)
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %00111100
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00000000
; Z (90)
    dc.b    %01111110
    dc.b    %00000110
    dc.b    %00001100
    dc.b    %00011000
    dc.b    %00110000
    dc.b    %01100000
    dc.b    %01111110
    dc.b    %00000000
; [ (91)
    dc.b    %00111100
    dc.b    %00110000
    dc.b    %00110000
    dc.b    %00110000
    dc.b    %00110000
    dc.b    %00110000
    dc.b    %00111100
    dc.b    %00000000
; \ (92)
    dc.b    %10000000
    dc.b    %01000000
    dc.b    %00100000
    dc.b    %00010000
    dc.b    %00001000
    dc.b    %00000100
    dc.b    %00000010
    dc.b    %00000000
; ] (93)
    dc.b    %00111100
    dc.b    %00001100
    dc.b    %00001100
    dc.b    %00001100
    dc.b    %00001100
    dc.b    %00001100
    dc.b    %00111100
    dc.b    %00000000
; ^ (94)
    dc.b    %00011000
    dc.b    %00111100
    dc.b    %01100110
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00000000
; _ (95)
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %01111110
    dc.b    %00000000
; ` (96)
    dc.b    %00110000
    dc.b    %00011000
    dc.b    %00001100
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00000000
; a (97)
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00111100
    dc.b    %00000110
    dc.b    %00111110
    dc.b    %01100110
    dc.b    %00111110
    dc.b    %00000000
; b (98)
    dc.b    %01100000
    dc.b    %01100000
    dc.b    %01111100
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01111100
    dc.b    %00000000
; c (99)
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00111100
    dc.b    %01100110
    dc.b    %01100000
    dc.b    %01100110
    dc.b    %00111100
    dc.b    %00000000
; d (100)
    dc.b    %00000110
    dc.b    %00000110
    dc.b    %00111110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %00111110
    dc.b    %00000000
; e (101)
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00111100
    dc.b    %01100110
    dc.b    %01111110
    dc.b    %01100000
    dc.b    %00111100
    dc.b    %00000000
; f (102)
    dc.b    %00011100
    dc.b    %00110000
    dc.b    %00110000
    dc.b    %01111100
    dc.b    %00110000
    dc.b    %00110000
    dc.b    %00110000
    dc.b    %00000000
; g (103)
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00111110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %00111110
    dc.b    %00000110
    dc.b    %00111100
; h (104)
    dc.b    %01100000
    dc.b    %01100000
    dc.b    %01111100
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %00000000
; i (105)
    dc.b    %00011000
    dc.b    %00000000
    dc.b    %00111000
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00111100
    dc.b    %00000000
; j (106)
    dc.b    %00001100
    dc.b    %00000000
    dc.b    %00011100
    dc.b    %00001100
    dc.b    %00001100
    dc.b    %00001100
    dc.b    %01101100
    dc.b    %00111000
; k (107)
    dc.b    %01100000
    dc.b    %01100000
    dc.b    %01100110
    dc.b    %01101100
    dc.b    %01111000
    dc.b    %01101100
    dc.b    %01100110
    dc.b    %00000000
; l (108)
    dc.b    %00111000
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00111100
    dc.b    %00000000
; m (109)
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %01100011
    dc.b    %01110111
    dc.b    %01111111
    dc.b    %01101011
    dc.b    %01100011
    dc.b    %00000000
; n (110)
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %01111100
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %00000000
; o (111)
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00111100
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %00111100
    dc.b    %00000000
; p (112)
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %01111100
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01111100
    dc.b    %01100000
    dc.b    %01100000
; q (113)
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00111110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %00111110
    dc.b    %00000110
    dc.b    %00000110
; r (114)
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %01111100
    dc.b    %01100110
    dc.b    %01100000
    dc.b    %01100000
    dc.b    %01100000
    dc.b    %00000000
; s (115)
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00111110
    dc.b    %01100000
    dc.b    %00111100
    dc.b    %00000110
    dc.b    %01111100
    dc.b    %00000000
; t (116)
    dc.b    %00110000
    dc.b    %00110000
    dc.b    %01111100
    dc.b    %00110000
    dc.b    %00110000
    dc.b    %00110000
    dc.b    %00011100
    dc.b    %00000000
; u (117)
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %00111110
    dc.b    %00000000
; v (118)
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %00111100
    dc.b    %00011000
    dc.b    %00000000
; w (119)
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %01100011
    dc.b    %01101011
    dc.b    %01111111
    dc.b    %01110111
    dc.b    %01100011
    dc.b    %00000000
; x (120)
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %01100110
    dc.b    %00111100
    dc.b    %00011000
    dc.b    %00111100
    dc.b    %01100110
    dc.b    %00000000
; y (121)
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %01100110
    dc.b    %00111110
    dc.b    %00000110
    dc.b    %00111100
; z (122)
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %01111110
    dc.b    %00001100
    dc.b    %00011000
    dc.b    %00110000
    dc.b    %01111110
    dc.b    %00000000
; { (123)
    dc.b    %00001100
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00110000
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00001100
    dc.b    %00000000
; | (124)
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00000000
; } (125)
    dc.b    %00110000
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00001100
    dc.b    %00011000
    dc.b    %00011000
    dc.b    %00110000
    dc.b    %00000000
; ~ (126)
    dc.b    %00110010
    dc.b    %01001100
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00000000
    dc.b    %00000000

    even
