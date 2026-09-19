; ============================================================
; autoconfig_glue.s - register ABI shim for autoconfig.c
; ============================================================

    xref    _rom_configure_zorro_ii

; configure_zorro_ii - D0.l = base of the first memory card, 0 if none
configure_zorro_ii:
    movem.l d1-d7/a0-a6,-(sp)
    jsr     _rom_configure_zorro_ii
    movem.l (sp)+,d1-d7/a0-a6
    rts
