; ============================================================
; serial_glue.s - register ABI shims for serial.c
; ============================================================
; panic.s, debugger.s and bootstrap.s call these with values in D0 and
; pointers in A0, and expect everything else to survive. vbcc clobbers
; d0/d1/a0/a1 across a call, so each shim saves them.
;
; The shims are stricter than the contracts they replace - the assembly
; documented D0/D1/A0 as scratch - which can only make a caller safer.
; ============================================================

    xref    _rom_serial_init
    xref    _rom_serial_put_char
    xref    _rom_serial_put_string
    xref    _rom_serial_get_char
    xref    _rom_serial_wait_char
    xref    _rom_serial_put_hex
    xref    _rom_serial_put_decimal

; serial_init - no parameters, preserves all registers
serial_init:
    movem.l d0-d1/a0-a1,-(sp)
    jsr     _rom_serial_init
    movem.l (sp)+,d0-d1/a0-a1
    rts

; serial_put_char - D0.b = character
serial_put_char:
    movem.l d0-d1/a0-a1,-(sp)
    move.l  d0,-(sp)
    jsr     _rom_serial_put_char
    addq.l  #4,sp
    movem.l (sp)+,d0-d1/a0-a1
    rts

; serial_put_string - A0 = NUL-terminated string
serial_put_string:
    movem.l d0-d1/a0-a1,-(sp)
    move.l  a0,-(sp)
    jsr     _rom_serial_put_string
    addq.l  #4,sp
    movem.l (sp)+,d0-d1/a0-a1
    rts

; serial_get_char - non-blocking. D0.b = character, or 0 with Z set.
serial_get_char:
    movem.l d1/a0-a1,-(sp)
    jsr     _rom_serial_get_char
    movem.l (sp)+,d1/a0-a1
    tst.b   d0                  ; Z set when nothing was waiting
    rts

; serial_wait_char - blocking. D0.b = character.
serial_wait_char:
    movem.l d1/a0-a1,-(sp)
    jsr     _rom_serial_wait_char
    movem.l (sp)+,d1/a0-a1
    rts

; serial_put_hex8/16/32 - D0 = value, 2/4/8 digits
serial_put_hex8:
    moveq   #2,d1
    bra.s   put_hex_common
serial_put_hex16:
    moveq   #4,d1
    bra.s   put_hex_common
serial_put_hex32:
    moveq   #8,d1
put_hex_common:
    movem.l d0-d1/a0-a1,-(sp)
    move.l  d1,-(sp)            ; rom_serial_put_hex(value, digits)
    move.l  d0,-(sp)
    jsr     _rom_serial_put_hex
    addq.l  #8,sp
    movem.l (sp)+,d0-d1/a0-a1
    rts

; serial_put_decimal - D0 = value, unsigned, full 32-bit range
serial_put_decimal:
    movem.l d0-d1/a0-a1,-(sp)
    move.l  d0,-(sp)
    jsr     _rom_serial_put_decimal
    addq.l  #4,sp
    movem.l (sp)+,d0-d1/a0-a1
    rts
