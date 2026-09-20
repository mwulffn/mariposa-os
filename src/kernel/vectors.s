; ============================================================
; vectors.s - interrupt and exception entry stubs that call C
; ============================================================
; Unlike cpu.s and isr.s this is not position independent and is not
; assembled standalone: it references C. The tests reach it through the
; real kernel image instead - see tests/test_kserial.c.
; ============================================================

        section .text

        xdef    _ser_tbe_handler
        xdef    _trap_init

        xref    _ser_tbe_isr
        xref    _ser_flush
        xref    isr_exit
        xref    _isr_depth

; ------------------------------------------------------------
; ser_tbe_handler - level 1 autovector, serial transmit buffer empty
; ------------------------------------------------------------
; Raises the mask to 7 before touching anything. The 68000 has only raised
; it to 1, so a vertical blank handler that calls kprintf would otherwise
; walk into the ring buffer while this is halfway through moving its tail.
; The body is a few dozen instructions; nothing is kept waiting long.
;
; D0/D1/A0/A1 are the registers vbcc treats as scratch, so they are the
; ones a call into C can destroy.
_ser_tbe_handler:
        or.w    #$0700,sr
        addq.b  #1,_isr_depth
        movem.l d0-d1/a0-a1,-(sp)
        jsr     _ser_tbe_isr
        movem.l (sp)+,d0-d1/a0-a1
        subq.b  #1,_isr_depth
        jmp     isr_exit            ; every handler leaves this way: switch.s

; ------------------------------------------------------------
; Crash stubs - get queued serial output out before the ROM panics
; ------------------------------------------------------------
; The ROM's panic handler bangs the UART directly and knows nothing about
; the kernel's ring buffer. Left alone, a crash prints the register dump and
; drops up to a second of the output that led to it - the lines that matter
; most. So the kernel wraps the fault vectors: flush, then carry on into
; whatever handler was there before, with the exception frame untouched.
;
; One stub per vector, because the stub is the only record of which vector
; fired. Each pushes the handler it replaced; the common tail flushes and
; RTSes into it. At that RTS the stack is exactly the frame the CPU built -
; six bytes or fourteen - so the ROM decodes it as if nothing intervened.

FIRST_VEC       equ 2               ; bus error
LAST_VEC        equ 11              ; line F
NUM_VECS        equ LAST_VEC-FIRST_VEC+1

crash_stub      macro
        move.l  old_vectors+(\1*4)(pc),-(sp)
        bra     crash_common
        endm

crash_stubs:
        crash_stub 0
crash_stub_end:
        crash_stub 1
        crash_stub 2
        crash_stub 3
        crash_stub 4
        crash_stub 5
        crash_stub 6
        crash_stub 7
        crash_stub 8
        crash_stub 9

STUB_SIZE       equ crash_stub_end-crash_stubs

crash_common:
        or.w    #$0700,sr
        movem.l d0-d1/a0-a1,-(sp)

        ; A fault inside the flush would come straight back here. Second
        ; time through, skip it and let the ROM have the machine.
        lea     crash_busy(pc),a0
        tst.b   (a0)
        bne.s   .chain
        st      (a0)
        jsr     _ser_flush
.chain:
        movem.l (sp)+,d0-d1/a0-a1
        rts                         ; into the handler this stub replaced

; void trap_init(void **table) - wrap vectors 2-11
;
; table is where the vector table is: 0 on a 68000, VBR on anything later.
;
; Call after ser_init. Idempotent: a second call would otherwise save the
; stubs as "the old handlers" and chain them to themselves for ever.
_trap_init:
        lea     crash_installed(pc),a0
        tst.b   (a0)
        bne.s   .done
        st      (a0)

        move.l  a2,-(sp)
        move.l  8(sp),a0            ; table, past the saved A2
        lea     FIRST_VEC*4(a0),a0  ; vector slots
        lea     old_vectors(pc),a1
        lea     crash_stubs(pc),a2
        moveq   #NUM_VECS-1,d0
.loop:
        move.l  (a0),(a1)+          ; remember the ROM's handler
        move.l  a2,(a0)+            ; install ours
        lea     STUB_SIZE(a2),a2
        dbf     d0,.loop
        move.l  (sp)+,a2
.done:
        rts

; .text is writable - the kernel is a RAM image - and starts zeroed from the
; image rather than from crt0's .bss clear, as in isr.s.
old_vectors:
        dcb.l   NUM_VECS,0
crash_busy:
        dc.b    0
crash_installed:
        dc.b    0
        even
