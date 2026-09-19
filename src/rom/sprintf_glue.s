; ============================================================
; sprintf_glue.s - stack ABI shim for the C formatter
; ============================================================
; The formatting itself lives in sprintf.c. This is the part that cannot:
; turning the ROM's caller-pushed argument frame into the (format, args)
; pair the C function takes, and honouring the register contract the old
; assembly Sprintf advertised.
;
; Why a shim rather than a variadic C function:
;
;   - The ~40 call sites push arguments and `bsr SerialPrintf`. Leaving that
;     frame untouched means none of them have to change.
;   - vbcc clobbers d0/d1/a0/a1 across a call, but the assembly Sprintf
;     preserved everything except its return values. Callers were written
;     against that, so the shim saves d1-d7/a1-a6 and the contract holds.
;
; It also fixes a documented bug: the old Sprintf read its format string at a
; fixed 60(sp), an offset that only worked when SerialPrintf had reached it
; via bsr, so Sprintf could not be called directly (docs/rom/sprintf_api.md).
; Both entry points now compute their own frame, so both work either way.
; ============================================================

    xref    _rom_vsprintf

; ------------------------------------------------------------
; Sprintf - format into SPRINTF_BUFFER
; ------------------------------------------------------------
;   4(sp)  = format string
;   8(sp)+ = arguments, one longword slot each
; Returns:
;   A0   = SPRINTF_BUFFER
;   D0.l = string length
; Preserves d1-d7 and a1-a6.
; ------------------------------------------------------------
Sprintf:
    movem.l d1-d7/a1-a6,-(sp)   ; 13 longwords = 52 bytes
    lea     60(sp),a0           ; 52 saved + 4 return + 4 format = first arg
    move.l  56(sp),a1           ; format string
    move.l  a0,-(sp)            ; rom_vsprintf(fmt, args) - args pushed first
    move.l  a1,-(sp)
    jsr     _rom_vsprintf
    addq.l  #8,sp               ; caller cleans, as vbcc expects
    lea     SPRINTF_BUFFER,a0   ; d0 already holds the length
    movem.l (sp)+,d1-d7/a1-a6
    rts

; ------------------------------------------------------------
; SerialPrintf - format and write to the serial port
; ------------------------------------------------------------
; Same frame as Sprintf. Does not route through it: each computes its own
; argument pointer, which is what makes both callable directly.
; ------------------------------------------------------------
SerialPrintf:
    movem.l d1-d7/a1-a6,-(sp)
    lea     60(sp),a0
    move.l  56(sp),a1
    move.l  a0,-(sp)
    move.l  a1,-(sp)
    jsr     _rom_vsprintf
    addq.l  #8,sp
    lea     SPRINTF_BUFFER,a0
    bsr     serial_put_string
    movem.l (sp)+,d1-d7/a1-a6
    rts
