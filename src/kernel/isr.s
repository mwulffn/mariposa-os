; ============================================================
; isr.s - interrupt service routines
; ============================================================
; Position independent, like cpu.s: hardware registers are absolute in any
; world, and the counter is reached PC-relative, so the tests can assemble
; this standalone and load it anywhere.
;
; The counter lives here rather than in C because of that. It sits in .text,
; which is writable - the kernel is a RAM image - and starts at zero from
; the image rather than from crt0's .bss clear.
; ============================================================

INTREQ          equ $DFF09C
INTF_VERTB      equ $0020           ; bit 5

        section .text

        xdef    _vbl_handler
        xdef    _vbl_count

; ------------------------------------------------------------
; vbl_handler - level 3 autovector, vertical blank
; ------------------------------------------------------------
; Installed at vector $6C. The 68000 has already pushed SR and PC and raised
; the interrupt mask to 3, so this runs with VERTB and everything below it
; masked and needs no critical section of its own.
;
; Acknowledging is not optional and not a detail: INTREQ bit 5 stays set
; until it is written back, and the level stays asserted, so a handler that
; forgets is re-entered the instant it RTEs. The harness models that rather
; than papering over it - see irq.unacked_reenters.
;
; Writing INTREQ with bit 15 clear is the CLR direction, so $0020 clears
; VERTB and leaves every other pending bit alone.
_vbl_handler:
        move.l  a0,-(sp)

        move.w  #INTF_VERTB,INTREQ      ; ack first, then count

        lea     _vbl_count(pc),a0       ; PC-relative: no absolute data
        addq.l  #1,(a0)

        move.l  (sp)+,a0
        rte

; ------------------------------------------------------------
; vbl_count - vertical blanks seen since boot
; ------------------------------------------------------------
; Read by the kernel as `extern volatile unsigned long vbl_count`. A long is
; wide enough for 50Hz to run for two and a half years.
_vbl_count:
        dc.l    0
