; ============================================================
; switch.s - the context switch
; ============================================================
; One frame format and one way out, however the task was suspended - see
; docs/task_design.md. A suspended task's stack, from its saved SP up:
;
;       USP                 4 bytes
;       D0-D7/A0-A6        60 bytes
;       exception frame     6 bytes on a 68000, 8 or more after it
;
; Nothing here knows how big that last part is. It was built by the CPU (or
; by task_create, imitating it) and it is consumed by RTE, which is why the
; same code runs from a 68000 to a 68060.
;
; USP is saved although every task runs in supervisor mode today. Together
; with leaving by RTE - which restores the S bit from the saved SR like any
; other - it means a user-mode task would be a different initial SR and not
; a different context switch.
; ============================================================

INTREQ          equ $DFF09C
INTF_VERTB      equ $0020

        section .text

        xdef    _tick_handler
        xdef    _yield_handler
        xdef    _task_yield
        xdef    isr_exit
        xdef    _need_resched
        xdef    _isr_depth

        xref    _sched_tick
        xref    _sched_switch
        xref    _vbl_count

; ------------------------------------------------------------
; tick_handler - level 3 autovector, vertical blank, 50Hz
; ------------------------------------------------------------
; Masks everything for its short life: the scheduler's queues are also
; touched by wake_one/wake_all from handlers at other levels.
_tick_handler:
        or.w    #$0700,sr
        addq.b  #1,_isr_depth
        movem.l d0-d1/a0-a1,-(sp)

        move.w  #INTF_VERTB,INTREQ
        addq.l  #1,_vbl_count
        jsr     _sched_tick

        movem.l (sp)+,d0-d1/a0-a1
        subq.b  #1,_isr_depth
        ; fall through

; ------------------------------------------------------------
; isr_exit - how every interrupt handler leaves
; ------------------------------------------------------------
; Enter with the stack exactly as the CPU left it and every register
; restored. Switches task if someone asked AND this interrupt is returning
; to task level. The second half matters: an interrupt that interrupted
; another handler must go back to it, or that handler finishes on some
; other task's time, on some other task's stack.
;
; "Task level" is read off the frame: the interrupted code's SR is its
; first word on every 680x0, and its interrupt mask is the low three bits of
; that word's high byte. Zero means nothing was being serviced.
isr_exit:
        tst.b   _need_resched
        beq.s   isr_rte
        move.l  d0,-(sp)
        move.b  4(sp),d0
        and.b   #$07,d0
        movem.l (sp)+,d0            ; movem leaves the flags alone
        bne.s   isr_rte

        or.w    #$0700,sr
switch_context:
        movem.l d0-d7/a0-a6,-(sp)
        move.l  usp,a0
        move.l  a0,-(sp)

        move.l  sp,-(sp)
        jsr     _sched_switch       ; (outgoing sp) -> incoming sp
        move.l  d0,sp               ; the argument went with the old stack

        move.l  (sp)+,a0
        move.l  a0,usp
        movem.l (sp)+,d0-d7/a0-a6
isr_rte:
        rte

; ------------------------------------------------------------
; yield_handler - TRAP #0: switch now
; ------------------------------------------------------------
; The CPU has built the same frame an interrupt would, so this joins the
; same path - unconditionally. A task may trap with interrupts masked, and
; that is how blocking is made race free: mask, queue yourself, mark
; yourself blocked, trap. The incoming task's SR comes from its own frame,
; and this one gets its mask back when it is resumed.
_yield_handler:
        or.w    #$0700,sr
        bra.s   switch_context

; void task_yield(void)
_task_yield:
        trap    #0
        rts

; Set by anything that makes a better task runnable; cleared by the switch.
_need_resched:
        dc.b    0
; How many interrupt handlers deep. wake_* uses it to tell "woken from a
; task, switch now" from "woken from a handler, isr_exit will see to it".
_isr_depth:
        dc.b    0
        even
