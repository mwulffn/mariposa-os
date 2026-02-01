; libsup.s - vbcc support routines
;
; The 68000 lacks 32-bit divide/multiply instructions.
; vbcc generates calls to these routines.
;
; Calling convention:
;   D0 = dividend (or left operand)
;   D1 = divisor (or right operand)
;   Result in D0
;   D1 may be trashed

        section CODE

        xdef    __divu
        xdef    __divs
        xdef    __modu
        xdef    __mods

;---------------------------------------------------------------
; __divu - Unsigned 32-bit divide
; D0 / D1 -> D0 (quotient)
;---------------------------------------------------------------
__divu:
        tst.l   d1
        beq.s   .div_zero       ; Division by zero
        
        movem.l d2-d3,-(sp)
        
        move.l  d1,d3           ; d3 = divisor
        move.l  d0,d2           ; d2 = dividend
        
        ; Check if we can use hardware divide (divisor fits in 16 bits
        ; and quotient will fit in 16 bits)
        cmp.l   #$10000,d3
        bhs.s   .full_divide
        
        ; divisor < 65536, check if quotient fits in 16 bits
        move.l  d2,d0
        clr.w   d0
        swap    d0              ; d0 = high word of dividend
        cmp.l   d3,d0
        bhs.s   .full_divide
        
        ; Can use two hardware divides
        ; high_quot = (high_dividend << 16) / divisor
        ; low_quot = ((remainder << 16) | low_dividend) / divisor
        move.l  d2,d0
        divu    d3,d0           ; d0.w = quotient, d0 high = remainder
        bvs.s   .full_divide    ; Overflow, use slow path
        
        movem.l (sp)+,d2-d3
        and.l   #$FFFF,d0       ; Clear remainder, keep quotient
        rts

.full_divide:
        ; Full 32-bit division using shift-subtract
        moveq   #31,d0          ; Bit counter
        moveq   #0,d1           ; Remainder
        
.div_loop:
        lsl.l   #1,d2           ; Shift dividend left, MSB -> X
        roxl.l  #1,d1           ; Shift X into remainder
        cmp.l   d3,d1           ; Compare remainder with divisor
        blo.s   .div_next
        sub.l   d3,d1           ; Subtract divisor from remainder
        addq.l  #1,d2           ; Set quotient bit
.div_next:
        dbf     d0,.div_loop
        
        move.l  d2,d0           ; Quotient to D0
        movem.l (sp)+,d2-d3
        rts

.div_zero:
        ; Division by zero - return max value
        moveq   #-1,d0
        rts

;---------------------------------------------------------------
; __divs - Signed 32-bit divide
; D0 / D1 -> D0 (quotient)
;---------------------------------------------------------------
__divs:
        tst.l   d1
        beq.s   .sdiv_zero
        
        movem.l d2-d3,-(sp)
        
        moveq   #0,d2           ; Sign flag
        
        tst.l   d0
        bpl.s   .sdiv_pos1
        neg.l   d0
        not.b   d2
.sdiv_pos1:
        tst.l   d1
        bpl.s   .sdiv_pos2
        neg.l   d1
        not.b   d2
.sdiv_pos2:
        ; Now both operands positive, d2 = sign of result
        move.l  d2,-(sp)        ; Save sign
        bsr.s   __divu_inner
        move.l  (sp)+,d2
        
        tst.b   d2
        beq.s   .sdiv_done
        neg.l   d0
.sdiv_done:
        movem.l (sp)+,d2-d3
        rts

.sdiv_zero:
        tst.l   d0
        bpl.s   .sdiv_zpos
        move.l  #$80000000,d0   ; Negative / 0 = min int
        rts
.sdiv_zpos:
        move.l  #$7FFFFFFF,d0   ; Positive / 0 = max int
        rts

; Inner unsigned divide (called from __divs with regs already saved)
__divu_inner:
        move.l  d1,d3
        move.l  d0,d2
        
        cmp.l   #$10000,d3
        bhs.s   .full2
        
        move.l  d2,d0
        clr.w   d0
        swap    d0
        cmp.l   d3,d0
        bhs.s   .full2
        
        move.l  d2,d0
        divu    d3,d0
        bvs.s   .full2
        and.l   #$FFFF,d0
        rts

.full2:
        moveq   #31,d0
        moveq   #0,d1
.loop2:
        lsl.l   #1,d2
        roxl.l  #1,d1
        cmp.l   d3,d1
        blo.s   .next2
        sub.l   d3,d1
        addq.l  #1,d2
.next2:
        dbf     d0,.loop2
        move.l  d2,d0
        rts

;---------------------------------------------------------------
; __modu - Unsigned 32-bit modulo
; D0 % D1 -> D0 (remainder)
;---------------------------------------------------------------
__modu:
        tst.l   d1
        beq.s   .mod_zero
        
        movem.l d2-d3,-(sp)
        
        move.l  d1,d3           ; divisor
        move.l  d0,d2           ; dividend
        
        ; Try fast path
        cmp.l   #$10000,d3
        bhs.s   .mod_full
        
        move.l  d2,d0
        clr.w   d0
        swap    d0
        cmp.l   d3,d0
        bhs.s   .mod_full
        
        move.l  d2,d0
        divu    d3,d0
        bvs.s   .mod_full
        clr.w   d0
        swap    d0              ; Remainder in low word
        movem.l (sp)+,d2-d3
        rts

.mod_full:
        moveq   #31,d0
        moveq   #0,d1           ; Remainder
.mod_loop:
        lsl.l   #1,d2
        roxl.l  #1,d1
        cmp.l   d3,d1
        blo.s   .mod_next
        sub.l   d3,d1
.mod_next:
        dbf     d0,.mod_loop
        
        move.l  d1,d0           ; Remainder to D0
        movem.l (sp)+,d2-d3
        rts

.mod_zero:
        moveq   #0,d0
        rts

;---------------------------------------------------------------
; __mods - Signed 32-bit modulo
; D0 % D1 -> D0 (remainder)
;---------------------------------------------------------------
__mods:
        tst.l   d1
        beq.s   .smod_zero
        
        movem.l d2,-(sp)
        
        moveq   #0,d2           ; Sign of result = sign of dividend
        
        tst.l   d0
        bpl.s   .smod_pos1
        neg.l   d0
        not.b   d2
.smod_pos1:
        tst.l   d1
        bpl.s   .smod_pos2
        neg.l   d1
.smod_pos2:
        bsr.s   __modu
        
        tst.b   d2
        beq.s   .smod_done
        neg.l   d0
.smod_done:
        movem.l (sp)+,d2
        rts

.smod_zero:
        moveq   #0,d0
        rts
