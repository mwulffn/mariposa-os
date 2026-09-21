; ============================================================
; blkcopy.s - copy one 512-byte block
; ============================================================
; void blk_copy512(void *to, const void *from);
;
; The block cache copies a block on every hit, every miss and every write,
; and a C loop of longword moves costs about 5,000 cycles a block - which,
; with the disk loop fixed, made the cache the most expensive thing in a
; write. MOVEM moves 48 bytes for the price of about six long moves; ten of
; those and eight long moves are 512 bytes in roughly 1,500 cycles.
; Both addresses must be even. Position independent.
; ============================================================

        section .text

        xdef    _blk_copy512

_blk_copy512:
        move.l  4(sp),a1                ; to
        move.l  8(sp),a0                ; from   (read before the push moves SP)
        movem.l d2-d7/a2-a6,-(sp)
        moveq   #9,d0                   ; 10 x 48 = 480 bytes
.loop:  movem.l (a0)+,d1-d7/a2-a6       ; twelve registers
        movem.l d1-d7/a2-a6,(a1)
        lea     48(a1),a1
        dbf     d0,.loop
        moveq   #7,d0                   ; 8 x 4 = the last 32
.tail:  move.l  (a0)+,(a1)+
        dbf     d0,.tail
        movem.l (sp)+,d2-d7/a2-a6
        rts
