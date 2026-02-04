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
; D0.l = number to print
; Preserves all registers except D0, D1, A0, A1 (scratch)
serial_put_decimal:
    movem.l d2/a2,-(sp)

    lea     SPRINTF_BUFFER,a0
    move.l  a0,a1           ; Save start position

    ; Handle zero special case
    tst.l   d0
    bne.s   .convert
    move.b  #'0',(a0)+
    bra.s   .terminate

.convert:
    ; Convert to decimal (store digits in reverse)
    move.l  a0,a1           ; Mark start
.digit_loop:
    move.l  d0,d1
    divu    #10,d1          ; d1 = d0 / 10
    swap    d1              ; Remainder in low word
    add.b   #'0',d1         ; Convert to ASCII
    move.b  d1,(a0)+        ; Store digit
    clr.w   d1
    swap    d1
    move.l  d1,d0           ; Quotient becomes new dividend
    tst.l   d0
    bne.s   .digit_loop

    ; Reverse the string
    move.l  a0,a2           ; End position
    subq.l  #1,a2
.reverse_loop:
    cmp.l   a1,a2
    ble.s   .terminate
    move.b  (a1),d1
    move.b  (a2),(a1)+
    subq.l  #1,a2
    move.b  d1,(a2)
    bra.s   .reverse_loop

.terminate:
    clr.b   (a0)            ; Null terminate
    move.l  a1,a0
    bsr     serial_put_string

    movem.l (sp)+,d2/a2
    rts
