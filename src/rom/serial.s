; ============================================================
; serial.s - Serial port debug output
; ============================================================
; Provides serial output functions for debugging via FS-UAE
; Call SerialInit once during boot
; Then use SerialPutChar or SerialPutString for output
; ============================================================

; ============================================================
; SerialInit - Initialize serial port
; ============================================================
; No parameters, modifies A6 only
SerialInit:
    lea     CUSTOM,a6
    move.w  #$0170,SERPER(a6)       ; 9600 baud (368 decimal)
    rts

; ============================================================
; SerialPutChar - Send single character
; ============================================================
; D0.b = character to send
; Modifies: A6
SerialPutChar:
    movem.l d0/a6,-(sp)
    lea     CUSTOM,a6
.wait:
    btst    #SERDATR_TBE,SERDATR(a6)    ; Wait for TBE
    beq.s   .wait
    move.w  d0,SERDAT(a6)               ; Send character
    movem.l (sp)+,d0/a6
    rts

; ============================================================
; SerialPutString - Send null-terminated string
; ============================================================
; A0 = pointer to string
; Modifies: D0, A0 (preserved), A6
SerialPutString:
    movem.l d0/a0/a6,-(sp)
.loop:
    move.b  (a0)+,d0
    beq.s   .done
    bsr     SerialPutChar
    bra.s   .loop
.done:
    movem.l (sp)+,d0/a0/a6
    rts

; ============================================================
; SerialGetChar - Non-blocking read
; ============================================================
; Returns character in D0.b if available, 0 with Z flag if not
; Modifies: D0, A6
SerialGetChar:
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
; SerialWaitChar - Blocking read
; ============================================================
; Returns character in D0.b when available
; Modifies: D0, A6
SerialWaitChar:
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
; SerialPutHex8 - Print byte as 2 hex digits
; ============================================================
; D0.b = value to print
; Modifies: D0-D2, A6
SerialPutHex8:
    movem.l d0-d2,-(sp)

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
    bsr     SerialPutChar

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
    bsr     SerialPutChar

    movem.l (sp)+,d0-d2
    rts

; ============================================================
; SerialPutHex16 - Print word as 4 hex digits
; ============================================================
; D0.w = value to print
; Modifies: D0-D1, A6
SerialPutHex16:
    movem.l d0-d1,-(sp)

    move.w  d0,d1                   ; Save value
    lsr.w   #8,d0                   ; Get high byte
    bsr     SerialPutHex8
    move.b  d1,d0                   ; Get low byte
    bsr     SerialPutHex8

    movem.l (sp)+,d0-d1
    rts

; ============================================================
; SerialPutHex32 - Print 32-bit hex value (8 digits, no prefix)
; ============================================================
; D0.l = value to print
; Modifies: D0-D2, A0, A6
SerialPutHex32:
    movem.l d0-d2/a0,-(sp)
    lea     HEX_BUFFER,a0

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
; SerialPutDecimal - Print decimal number
; ============================================================
; D0.l = number to print
; Modifies: D0-D2, A0-A2, A6
SerialPutDecimal:
    movem.l d0-d2/a0-a2,-(sp)

    lea     DEC_BUFFER,a0
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
    move.b  d1,(a2)-
    bra.s   .reverse_loop

.terminate:
    clr.b   (a0)            ; Null terminate
    move.l  a1,a0
    bsr     SerialPutString

    movem.l (sp)+,d0-d2/a0-a2
    rts
