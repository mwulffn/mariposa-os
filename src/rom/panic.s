; ============================================================
; panic.s - Serial-only panic handler for ROM
; ============================================================
; Provides panic() function that:
;   1. Saves all registers
;   2. Outputs CPU state to serial port
;   3. Enters interactive debugger
;
; Call with:  JSR panic
; Or install as exception handler via panic_with_msg
; ============================================================

; hardware.i already included by bootstrap.s

; ============================================================
; Register save area (fixed location in chip RAM)
; ============================================================
; Uses REG_DUMP_AREA from hardware.i ($400)

saved_regs      equ REG_DUMP_AREA
saved_d0        equ REG_DUMP_AREA+$00
saved_d1        equ REG_DUMP_AREA+$04
saved_d2        equ REG_DUMP_AREA+$08
saved_d3        equ REG_DUMP_AREA+$0C
saved_d4        equ REG_DUMP_AREA+$10
saved_d5        equ REG_DUMP_AREA+$14
saved_d6        equ REG_DUMP_AREA+$18
saved_d7        equ REG_DUMP_AREA+$1C
saved_a0        equ REG_DUMP_AREA+$20
saved_a1        equ REG_DUMP_AREA+$24
saved_a2        equ REG_DUMP_AREA+$28
saved_a3        equ REG_DUMP_AREA+$2C
saved_a4        equ REG_DUMP_AREA+$30
saved_a5        equ REG_DUMP_AREA+$34
saved_a6        equ REG_DUMP_AREA+$38
saved_a7        equ REG_DUMP_AREA+$3C
saved_sr        equ REG_DUMP_AREA+$40
saved_pc        equ REG_DUMP_AREA+$44
panic_msg_ptr   equ REG_DUMP_AREA+$48

; ============================================================
; panic - Main entry point
; ============================================================
; Call this to dump state and enter debugger.
; All registers are preserved for display.
; ============================================================
panic:
    ; Save registers immediately before we trash anything
    movem.l d0-d7/a0-a6,saved_regs

    ; Save A7 (stack pointer) - it's not in movem range
    move.l  sp,saved_a7

    ; Save SR
    move.w  sr,saved_sr

    ; Save return address as PC (caller's location)
    move.l  (sp),saved_pc

    ; Output to serial port
    bsr     panic_serial_output

    ; Enter interactive debugger
    jmp     debugger_main

; ============================================================
; panic_with_msg - Entry point preserving a message
; ============================================================
; A0 = pointer to message string (null terminated)
; Saves message pointer, then calls panic
; ============================================================
panic_with_msg:
    move.l  a0,panic_msg_ptr
    bra.s   panic

; ============================================================
; panic_serial_output - Send register dump to serial port
; ============================================================
panic_serial_output:
    movem.l d0-d7/a0-a6,-(sp)

    ; Send header
    lea     .header(pc),a0
    bsr     serial_put_string

    ; Send all data registers (D0-D7)
    lea     .lblD0(pc),a0
    bsr     serial_put_string
    move.l  saved_d0,d0
    bsr     serial_put_hex32

    lea     .lblD1(pc),a0
    bsr     serial_put_string
    move.l  saved_d1,d0
    bsr     serial_put_hex32

    lea     .lblD2(pc),a0
    bsr     serial_put_string
    move.l  saved_d2,d0
    bsr     serial_put_hex32

    lea     .lblD3(pc),a0
    bsr     serial_put_string
    move.l  saved_d3,d0
    bsr     serial_put_hex32
    bsr     .crlf

    lea     .lblD4(pc),a0
    bsr     serial_put_string
    move.l  saved_d4,d0
    bsr     serial_put_hex32

    lea     .lblD5(pc),a0
    bsr     serial_put_string
    move.l  saved_d5,d0
    bsr     serial_put_hex32

    lea     .lblD6(pc),a0
    bsr     serial_put_string
    move.l  saved_d6,d0
    bsr     serial_put_hex32

    lea     .lblD7(pc),a0
    bsr     serial_put_string
    move.l  saved_d7,d0
    bsr     serial_put_hex32
    bsr     .crlf

    ; Send all address registers (A0-A7)
    lea     .lblA0(pc),a0
    bsr     serial_put_string
    move.l  saved_a0,d0
    bsr     serial_put_hex32

    lea     .lblA1(pc),a0
    bsr     serial_put_string
    move.l  saved_a1,d0
    bsr     serial_put_hex32

    lea     .lblA2(pc),a0
    bsr     serial_put_string
    move.l  saved_a2,d0
    bsr     serial_put_hex32

    lea     .lblA3(pc),a0
    bsr     serial_put_string
    move.l  saved_a3,d0
    bsr     serial_put_hex32
    bsr     .crlf

    lea     .lblA4(pc),a0
    bsr     serial_put_string
    move.l  saved_a4,d0
    bsr     serial_put_hex32

    lea     .lblA5(pc),a0
    bsr     serial_put_string
    move.l  saved_a5,d0
    bsr     serial_put_hex32

    lea     .lblA6(pc),a0
    bsr     serial_put_string
    move.l  saved_a6,d0
    bsr     serial_put_hex32

    lea     .lblA7(pc),a0
    bsr     serial_put_string
    move.l  saved_a7,d0
    bsr     serial_put_hex32
    bsr     .crlf

    ; Send PC and SR
    lea     .lblPC(pc),a0
    bsr     serial_put_string
    move.l  saved_pc,d0
    bsr     serial_put_hex32

    lea     .lblSR(pc),a0
    bsr     serial_put_string
    move.w  saved_sr,d0
    bsr     serial_put_hex16
    bsr     .crlf

    ; Send custom message if present
    move.l  panic_msg_ptr,d0
    beq.s   .done
    bsr     .crlf
    move.l  d0,a0
    bsr     serial_put_string
    bsr     .crlf

.done:
    movem.l (sp)+,d0-d7/a0-a6
    rts

.crlf:
    move.b  #10,d0
    bsr     serial_put_char
    move.b  #13,d0
    bsr     serial_put_char
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
