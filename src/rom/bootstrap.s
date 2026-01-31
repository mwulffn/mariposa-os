; ============================================================
; bootstrap.s - Main ROM entry point
; ============================================================
; Assembles with VASM: vasmm68k_mot -Fbin -I src/rom -o kick.rom bootstrap.s
; ============================================================

    include "hardware.i"

; Panic and PanicFont are defined in debug.s which is included at the end

; ============================================================
; ROM header
; ============================================================
    org $FC0000

RomStart:
    dc.l    $3FFC               ; Offset 0: Initial SSP (temporary boot stack)
    dc.l    Start               ; Offset 4: Initial PC
    dc.l    ROM_MAGIC           ; Offset 8: Magic 'AMAG'
    dc.w    ROM_VERSION         ; Offset 12: Version 0.1
    dc.w    ROM_FLAGS           ; Offset 14: Flags

; ============================================================
; Entry point
; ============================================================
Start:
    ; ============================================================
    ; 1. HARDWARE INIT
    ; ============================================================
    ; Disable interrupts at CPU level
    move.w  #$2700,sr

    ; Point to custom chips
    lea     CUSTOM,a6

    ; Disable all DMA and interrupts
    move.w  #$7FFF,DMACON(a6)
    move.w  #$7FFF,INTENA(a6)
    move.w  #$7FFF,INTREQ(a6)

    ; Disable ROM overlay so chip RAM is visible at $0
    move.b  #$03,CIAA_DDRA
    move.b  #$00,CIAA_PRA

    ; Initialize CIAs to known state
    move.b  #$00,CIAA_CRA       ; Stop Timer A
    move.b  #$00,CIAA_CRB       ; Stop Timer B
    move.b  #$7F,CIAA_ICR       ; Clear all CIA-A interrupt enables
    move.b  #$00,CIAB_CRA       ; Stop Timer A
    move.b  #$00,CIAB_CRB       ; Stop Timer B
    move.b  #$7F,CIAB_ICR       ; Clear all CIA-B interrupt enables

    ; ============================================================
    ; 2. EXCEPTION VECTOR INSTALLATION
    ; ============================================================
    bsr     InstallExceptionVectors

    ; ============================================================
    ; 3. MEMORY DETECTION
    ; ============================================================
    bsr     DetectChipRAM       ; Returns size in d0
    move.l  d0,CHIP_RAM_VAR

    bsr     TestChipRAM         ; Test chip RAM (halts on yellow if fail)

    bsr     DetectFastRAM       ; Returns size in d0
    move.l  d0,FAST_RAM_VAR

    bsr     BuildMemoryMap

    ; ============================================================
    ; 4. SERIAL INIT
    ; ============================================================
    bsr     SerialInit

    ; Print version banner
    lea     BannerMsg(pc),a0
    bsr     SerialPutString

    ; Print detected chip RAM
    lea     ChipRAMMsg(pc),a0
    bsr     SerialPutString
    move.l  CHIP_RAM_VAR,d0
    bsr     SerialPutHex
    lea     BytesMsg(pc),a0
    bsr     SerialPutString

    ; Print detected fast RAM
    lea     FastRAMMsg(pc),a0
    bsr     SerialPutString
    move.l  FAST_RAM_VAR,d0
    bsr     SerialPutHex
    lea     BytesMsg(pc),a0
    bsr     SerialPutString

    ; ============================================================
    ; 5. SUCCESS HALT - BRIGHT GREEN SCREEN
    ; ============================================================
    ; Set up display
    bsr     InitDisplay

    ; Set COLOR00 to bright green
    move.w  #$0F0,COLOR00(a6)

    ; Print success message to serial
    lea     SuccessMsg(pc),a0
    bsr     SerialPutString

    ; Infinite loop
.halt:
    bra.s   .halt

; ============================================================
; InstallExceptionVectors - Install all exception handlers
; ============================================================
InstallExceptionVectors:
    movem.l a0-a1,-(sp)

    ; Install specific exception handlers
    move.l  #BusErrorHandler,VEC_BUS_ERROR
    move.l  #AddressErrorHandler,VEC_ADDR_ERROR
    move.l  #IllegalHandler,VEC_ILLEGAL
    move.l  #ZeroDivideHandler,VEC_ZERO_DIV
    move.l  #CHKHandler,VEC_CHK
    move.l  #TRAPVHandler,VEC_TRAPV
    move.l  #PrivilegeHandler,VEC_PRIV_VIOL
    move.l  #TraceHandler,VEC_TRACE
    move.l  #LineAHandler,VEC_LINE_A
    move.l  #LineFHandler,VEC_LINE_F
    move.l  #SpuriousHandler,VEC_SPURIOUS

    ; Install autovector handlers (IRQ 1-7)
    move.l  #AutoVecHandler,VEC_AUTOVEC1
    move.l  #AutoVecHandler,VEC_AUTOVEC2
    move.l  #AutoVecHandler,VEC_AUTOVEC3
    move.l  #AutoVecHandler,VEC_AUTOVEC4
    move.l  #AutoVecHandler,VEC_AUTOVEC5
    move.l  #AutoVecHandler,VEC_AUTOVEC6
    move.l  #AutoVecHandler,VEC_AUTOVEC7

    ; Install TRAP handlers (0-15)
    movem.l d0,-(sp)
    lea     VEC_TRAP0,a0
    moveq   #15,d0
.trap_loop:
    move.l  #TrapHandler,(a0)+
    dbf     d0,.trap_loop

    ; Install generic handler for remaining vectors
    lea     $0030,a0            ; Start after reserved vectors
    move.w  #(256-12-1),d0      ; Remaining vectors
.generic_loop:
    move.l  #GenericHandler,(a0)+
    dbf     d0,.generic_loop

    movem.l (sp)+,d0

    movem.l (sp)+,a0-a1
    rts

; ============================================================
; Exception Handlers
; ============================================================
BusErrorHandler:
    lea     BusErrorMsg(pc),a0
    jmp     PanicWithMsg

AddressErrorHandler:
    lea     AddrErrorMsg(pc),a0
    jmp     PanicWithMsg

IllegalHandler:
    lea     IllegalMsg(pc),a0
    jmp     PanicWithMsg

ZeroDivideHandler:
    lea     ZeroDivMsg(pc),a0
    jmp     PanicWithMsg

CHKHandler:
    lea     CHKMsg(pc),a0
    jmp     PanicWithMsg

TRAPVHandler:
    lea     TRAPVMsg(pc),a0
    jmp     PanicWithMsg

PrivilegeHandler:
    lea     PrivMsg(pc),a0
    jmp     PanicWithMsg

TraceHandler:
    lea     TraceMsg(pc),a0
    jmp     PanicWithMsg

LineAHandler:
    lea     LineAMsg(pc),a0
    jmp     PanicWithMsg

LineFHandler:
    lea     LineFMsg(pc),a0
    jmp     PanicWithMsg

SpuriousHandler:
    lea     SpuriousMsg(pc),a0
    jmp     PanicWithMsg

AutoVecHandler:
    lea     AutoVecMsg(pc),a0
    jmp     PanicWithMsg

TrapHandler:
    lea     TrapMsg(pc),a0
    jmp     PanicWithMsg

GenericHandler:
    lea     GenericExcMsg(pc),a0
    jmp     PanicWithMsg

; ============================================================
; InitDisplay - Set up basic display for green screen
; ============================================================
InitDisplay:
    movem.l d0/a0/a6,-(sp)
    lea     CUSTOM,a6

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
    move.w  #$0000,BPL1MOD(a6)
    move.w  #$0000,BPL2MOD(a6)

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

    ; Start copper
    move.l  #COPPERLIST,d0
    move.w  d0,COP1LCL(a6)
    swap    d0
    move.w  d0,COP1LCH(a6)
    move.w  COPJMP1(a6),d0

    ; Enable DMA
    move.w  #$8380,DMACON(a6)       ; SET + BPLEN + COPEN + DMAEN

    movem.l (sp)+,d0/a0/a6
    rts

; ============================================================
; InitCopper - Build copper list
; ============================================================
InitCopper:
    movem.l a0,-(sp)
    lea     COPPERLIST,a0

    move.w  #BPL1PTH,(a0)+
    move.w  #(SCREEN>>16)&$FFFF,(a0)+
    move.w  #BPL1PTL,(a0)+
    move.w  #SCREEN&$FFFF,(a0)+

    move.l  #$FFFFFFFE,(a0)+        ; End

    movem.l (sp)+,a0
    rts

; ============================================================
; SerialPutHex - Print 32-bit hex value
; d0.l = value to print
; ============================================================
SerialPutHex:
    movem.l d0-d2/a0,-(sp)
    lea     HEX_BUFFER,a0
    move.b  #'$',(a0)+

    moveq   #7,d2               ; 8 hex digits
.loop:
    rol.l   #4,d0
    move.l  d0,d1
    and.w   #$0F,d1
    cmp.b   #10,d1
    blt.s   .digit
    add.b   #'A'-10,d1
    bra.s   .store
.digit:
    add.b   #'0',d1
.store:
    move.b  d1,(a0)+
    dbf     d2,.loop

    clr.b   (a0)                ; Null terminate
    lea     HEX_BUFFER,a0
    bsr     SerialPutString

    movem.l (sp)+,d0-d2/a0
    rts

; ============================================================
; Data
; ============================================================
BannerMsg:
    dc.b    "AMAG ROM v0.1",10,13,0
    even

ChipRAMMsg:
    dc.b    "Chip RAM: ",0
    even

FastRAMMsg:
    dc.b    "Fast RAM: ",0
    even

BytesMsg:
    dc.b    " bytes",10,13,0
    even

SuccessMsg:
    dc.b    "Boot success - GREEN SCREEN",10,13,0
    even

BusErrorMsg:
    dc.b    "BUS ERROR",0
    even

AddrErrorMsg:
    dc.b    "ADDRESS ERROR",0
    even

IllegalMsg:
    dc.b    "ILLEGAL INSTRUCTION",0
    even

ZeroDivMsg:
    dc.b    "DIVIDE BY ZERO",0
    even

CHKMsg:
    dc.b    "CHK EXCEPTION",0
    even

TRAPVMsg:
    dc.b    "TRAPV EXCEPTION",0
    even

PrivMsg:
    dc.b    "PRIVILEGE VIOLATION",0
    even

TraceMsg:
    dc.b    "TRACE EXCEPTION",0
    even

LineAMsg:
    dc.b    "LINE-A EXCEPTION",0
    even

LineFMsg:
    dc.b    "LINE-F EXCEPTION",0
    even

SpuriousMsg:
    dc.b    "SPURIOUS INTERRUPT",0
    even

AutoVecMsg:
    dc.b    "AUTOVECTOR INTERRUPT",0
    even

TrapMsg:
    dc.b    "TRAP EXCEPTION",0
    even

GenericExcMsg:
    dc.b    "UNKNOWN EXCEPTION",0
    even

; ============================================================
; Include debug, memory, and serial modules
; ============================================================
    include "debug.s"
    include "memory.s"
    include "serial.s"

; ============================================================
; ROM footer - pad to 256KB and add checksum location
; ============================================================
    org $FFFFFC
RomEnd:
    dc.l    RomStart

    end
