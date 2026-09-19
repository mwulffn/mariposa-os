; ============================================================
; ide_glue.s - register ABI shims for ide.c
; ============================================================
; partition.s and filesystem.s call ide_read with the buffer in A0, the LBA
; in D0 and the sector count in D1.w, and expect D2-D7/A2-A6 to survive.
; vbcc clobbers d0/d1/a0/a1, which the documented contract already allowed.
; ============================================================

    xref    _rom_ide_read
    xref    _rom_ide_test_read

; ------------------------------------------------------------
; ide_read - read sectors
;   A0.l = destination buffer (word aligned)
;   D0.l = starting LBA
;   D1.w = sector count, 1-256
; Returns D0.l = 0 success, -1 error. Preserves D2-D7/A2-A6.
; ------------------------------------------------------------
ide_read:
    movem.l d2/a2,-(sp)
    moveq   #0,d2
    move.w  d1,d2               ; widen the count; a word of 0 stays 0
    move.l  d2,-(sp)            ; rom_ide_read(buf, lba, count)
    move.l  d0,-(sp)
    move.l  a0,-(sp)
    jsr     _rom_ide_read
    lea     12(sp),sp
    movem.l (sp)+,d2/a2
    rts

; ------------------------------------------------------------
; ide_test_read - read sector 0 to $30000 and report
; Returns D0.l = 0 success, -1 error. Preserves everything else.
; ------------------------------------------------------------
ide_test_read:
    movem.l d1-d7/a0-a6,-(sp)
    jsr     _rom_ide_test_read
    movem.l (sp)+,d1-d7/a0-a6
    rts
