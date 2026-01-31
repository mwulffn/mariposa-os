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
