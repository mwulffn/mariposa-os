; ============================================================
; dcon_draw.s - the boot console's inner loop
; ============================================================
; void dcon_draw_cells(unsigned char *p0, unsigned char *p1,
;                      const char *chars, const unsigned char *colours,
;                      unsigned long row_step, long cursor_col);
;
; Draw one row of DCON_COLS text cells into two bitplanes. p0 and p1 point
; at the top left byte of the row in each plane, and row_step is how far
; apart consecutive pixel rows of one plane are - NOT bytes per row: rows
; are interleaved (docs/bitmap_design.md). colours[] is one byte a
; cell: bit 0 says the glyph goes in plane 0, bit 1 in plane 1. cursor_col
; is drawn as a solid block in both planes; -1 for no cursor.
;
; In assembly because it is the one loop in the console that matters and C
; could not be made to do it: vbcc's version cost about 2,700 cycles a cell,
; a second for a full screen. This is about 530 - eight byte-pairs a cell,
; unrolled, no multiplies, the glyph read sequentially.
; ============================================================

DCON_COLS       equ 80

        section .text

        xdef    _dcon_draw_cells
        xref    _font8x8

; one pixel row of one cell: glyph byte -> both planes, step down a line
pixrow  macro
        move.b  (a5)+,d0
        or.b    d4,d0                   ; cursor: all ones
        move.b  d0,d7
        and.b   d2,d0
        and.b   d3,d7
        move.b  d0,0(a3,d1.w)
        move.b  d7,0(a4,d1.w)
        add.w   d5,d1
        endm

_dcon_draw_cells:
        movem.l d2-d7/a2-a5,-(sp)       ; 40 bytes: arguments from 44(sp)
        move.l  44(sp),a3               ; plane 0
        move.l  48(sp),a4               ; plane 1
        move.l  52(sp),a0               ; characters
        move.l  56(sp),a1               ; colours
        move.l  60(sp),d5               ; row step
        move.l  64(sp),d6               ; cells until the cursor; never 0 if -1
        lea     _font8x8,a2
        move.w  #DCON_COLS-1,-(sp)      ; cell counter: every register is busy

.cell:  moveq   #0,d0
        move.b  (a0)+,d0
        lsl.w   #3,d0                   ; eight bytes a glyph
        lea     0(a2,d0.w),a5
        move.b  (a1)+,d1

        sf      d4
        tst.l   d6
        bne.s   .masks
        st      d4                      ; the cursor cell: solid, bright
        moveq   #3,d1
.masks: btst    #0,d1
        sne     d2                      ; $FF if the glyph goes in plane 0
        btst    #1,d1
        sne     d3                      ; ... in plane 1
        moveq   #0,d1                   ; offset of the current pixel row

        pixrow
        pixrow
        pixrow
        pixrow
        pixrow
        pixrow
        pixrow
        pixrow

        addq.l  #1,a3
        addq.l  #1,a4
        subq.l  #1,d6
        subq.w  #1,(sp)
        bpl     .cell

        addq.l  #2,sp
        movem.l (sp)+,d2-d7/a2-a5
        rts
