; ============================================================
; math.s - 32-bit arithmetic helpers
; ============================================================
; The 68000 has no 32x32 multiply and no 32-bit divide. What it has is
; mulu.w (16x16 -> 32) and divu.w (32/16 -> 16), and both fail quietly
; when a value outgrows its 16 bits: chained mulu.w truncates the
; intermediate, and divu.w on overflow leaves the destination untouched
; and only sets V. Routines that need full 32-bit range call these.
; ============================================================

; ============================================================
; mul32x16 - Multiply a 32-bit value by a 16-bit value
; ============================================================
; product = (hi * m) << 16  +  lo * m, keeping the low 32 bits.
;
; Input:  D0.l = multiplicand
;         D1.w = multiplier (high word ignored)
; Output: D0.l = low 32 bits of the product
; Preserves: D1-D7, A0-A6
; ============================================================
mul32x16:
    move.l  d2,-(sp)

    move.l  d0,d2
    swap    d2                  ; D2.w = high word of the multiplicand
    mulu.w  d1,d2               ; high * multiplier
    swap    d2
    clr.w   d2                  ; keep only the part that lands above bit 15
    mulu.w  d1,d0               ; low * multiplier
    add.l   d2,d0

    move.l  (sp)+,d2
    rts

; ============================================================
; divu32_10 - Divide a 32-bit unsigned value by 10
; ============================================================
; Two divu.w steps, high half first, feeding that remainder into the low
; half. The second dividend is (remainder << 16) | low, and since the
; remainder is at most 9 that is always under 655360 - so its quotient
; fits in 16 bits and divu.w cannot overflow.
;
; Input:  D0.l = dividend
; Output: D0.l = quotient
;         D1.l = remainder (0-9)
; Preserves: D2-D7, A0-A6
; ============================================================
divu32_10:
    move.l  d2,-(sp)

    move.l  d0,d2
    clr.w   d2
    swap    d2                  ; D2 = high word of the dividend, zero extended
    divu.w  #10,d2              ; D2.w = quotient, high half = remainder

    move.l  d2,d1
    swap    d1
    and.l   #$FFFF,d1           ; D1 = remainder of the high half (0-9)
    and.l   #$FFFF,d2           ; D2 = high word of the quotient

    swap    d1                  ; D1 = remainder << 16
    move.w  d0,d1               ; ... | low word of the dividend
    divu.w  #10,d1              ; dividend < 655360, so this cannot overflow

    swap    d2                  ; D2 = quotient high word, back in place
    move.w  d1,d2               ; ... | quotient low word

    clr.w   d1
    swap    d1                  ; D1 = final remainder

    move.l  d2,d0
    move.l  (sp)+,d2
    rts
