; ============================================================
; serial.s - Serial port debug output
; ============================================================
; Provides serial output functions for debugging via FS-UAE
; Call SerialInit once during boot
; Then use SerialPutChar or SerialPutString for output
; ============================================================

; ============================================================
; serial_init - Initialize serial port
; ============================================================
; No parameters, preserves all registers
serial_init:
    movem.l a6,-(sp)
    lea     CUSTOM,a6
    move.w  #$0170,SERPER(a6)       ; 9600 baud (368 decimal)
    movem.l (sp)+,a6
    rts

; ============================================================
; serial_put_char - Send single character
; ============================================================
; D0.b = character to send
; Preserves all registers except D0 (scratch)
serial_put_char:
    movem.l a6,-(sp)
    lea     CUSTOM,a6
.wait:
    btst    #SERDATR_TSRE,SERDATR(a6)    ; Wait for TBE
    beq.s   .wait
    move.w  d0,SERDAT(a6)               ; Send character
    movem.l (sp)+,a6
    rts

; ============================================================
; serial_put_string - Send null-terminated string
; ============================================================
; A0 = pointer to string
; Preserves all registers except D0, A0 (scratch)
serial_put_string:
    movem.l a6,-(sp)
.loop:
    move.b  (a0)+,d0
    beq.s   .done
    bsr     serial_put_char
    bra.s   .loop
.done:
    movem.l (sp)+,a6
    rts

; ============================================================
; serial_get_char - Non-blocking read
; ============================================================
; Returns character in D0.b if available, 0 with Z flag if not
; Preserves all registers except D0 (return value)
serial_get_char:
    movem.l a6,-(sp)
    lea     CUSTOM,a6
    btst    #SERDATR_RBF,SERDATR(a6)    ; Check receive buffer full
    beq.s   .no_data
    move.w  SERDATR(a6),d0              ; Read character + status
    and.w   #$00FF,d0                   ; Mask to byte, clears Z
    movem.l (sp)+,a6
    rts
.no_data:
    moveq   #0,d0                       ; Return 0 with Z flag set
    movem.l (sp)+,a6
    rts

; ============================================================
; serial_wait_char - Blocking read
; ============================================================
; Returns character in D0.b when available
; Preserves all registers except D0 (return value)
serial_wait_char:
    movem.l a6,-(sp)
    lea     CUSTOM,a6
.wait:
    btst    #SERDATR_RBF,SERDATR(a6)
    beq.s   .wait
    move.w  SERDATR(a6),d0
    and.w   #$00FF,d0                   ; Mask to byte
    movem.l (sp)+,a6
    rts

; ============================================================
; serial_put_hex8 - Print byte as 2 hex digits
; ============================================================
; D0.b = value to print
; Preserves all registers except D0, D1 (scratch)
serial_put_hex8:
    movem.l d2,-(sp)

    move.b  d0,d2                       ; Save value

    ; High nibble
    lsr.b   #4,d0
    and.b   #$0F,d0
    cmp.b   #10,d0
    blt.s   .digit1
    add.b   #'A'-10,d0
    bra.s   .send1
.digit1:
    add.b   #'0',d0
.send1:
    bsr     serial_put_char

    ; Low nibble
    move.b  d2,d0
    and.b   #$0F,d0
    cmp.b   #10,d0
    blt.s   .digit2
    add.b   #'A'-10,d0
    bra.s   .send2
.digit2:
    add.b   #'0',d0
.send2:
    bsr     serial_put_char

    movem.l (sp)+,d2
    rts

; ============================================================
; serial_put_hex16 - Print word as 4 hex digits
; ============================================================
; D0.w = value to print
; Preserves all registers except D0, D1 (scratch)
serial_put_hex16:
    move.w  d0,d1                   ; Save value
    lsr.w   #8,d0                   ; Get high byte
    bsr     serial_put_hex8
    move.b  d1,d0                   ; Get low byte
    bsr     serial_put_hex8
    rts

; ============================================================
; serial_put_hex32 - Print 32-bit hex value (8 digits, no prefix)
; ============================================================
; D0.l = value to print
; Preserves all registers except D0, D1, A0 (scratch)
serial_put_hex32:
    movem.l d2,-(sp)
    lea     SPRINTF_BUFFER,a0

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
    lea     SPRINTF_BUFFER,a0
    bsr     serial_put_string

    movem.l (sp)+,d2
    rts

; ============================================================
; serial_put_decimal - Print decimal number
; ============================================================
; D0.l = number to print (unsigned, full 32-bit range)
; Preserves all registers
serial_put_decimal:
    movem.l d0-d2/a0-a2,-(sp)

    lea     SPRINTF_BUFFER,a2   ; A2 = buffer start, never moves
    move.l  a2,a0               ; A0 = write pointer

    ; Handle zero special case
    tst.l   d0
    bne.s   .convert
    move.b  #'0',(a0)+
    clr.b   (a0)
    bra.s   .send

.convert:
    ; Digits come out least significant first. divu32_10 rather than
    ; divu.w #10, which is 32/16 -> 16 and overflows for any value from
    ; 655360 up, leaving its destination untouched.
.digit_loop:
    bsr     divu32_10           ; D0 = quotient, D1 = remainder
    add.b   #'0',d1
    move.b  d1,(a0)+
    tst.l   d0
    bne.s   .digit_loop

    clr.b   (a0)                ; Null terminate while A0 is still at the end

    ; Reverse in place: A1 walks forward from the first digit, A0 back from
    ; the last, and A2 still holds the start to print from. The old version
    ; decremented its tail pointer before storing the byte it had saved, so
    ; it wrote one place short, and then printed from the head pointer the
    ; loop had advanced - "4095" came out as "54".
    move.l  a2,a1
    subq.l  #1,a0
.reverse_loop:
    cmp.l   a1,a0
    bls.s   .send
    move.b  (a1),d1
    move.b  (a0),d2
    move.b  d2,(a1)+
    move.b  d1,(a0)
    subq.l  #1,a0
    bra.s   .reverse_loop

.send:
    move.l  a2,a0
    bsr     serial_put_string

    movem.l (sp)+,d0-d2/a0-a2
    rts
