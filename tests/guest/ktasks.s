; ============================================================
; ktasks.s - task bodies for the task.* tests
; ============================================================
; The scheduler is tested by running real tasks on it, and these are the
; tasks. Position independent and assembled standalone like the other
; modules; they know nothing about the kernel's addresses. Each gets a
; pointer to a control block the test built, and reaches the kernel through
; function pointers the test put there:
;
;    0  counter    bumped once per lap - how the test sees progress
;    4  fn         what to call each lap, if the body calls anything
;    8  param      argument for fn
;   12  aux        second pointer / error flag, per body
;   16  fn2        second function, per body
;   20  result     what fn2 last returned, for body_sampler
;
; vbcc ABI: argument at 4(sp) on entry, arguments pushed as longwords,
; D2-D7/A2-A6 survive a call.
; ============================================================

        xdef    body_yielder
        xdef    body_spinner
        xdef    body_masked_spinner
        xdef    body_sleeper
        xdef    body_once
        xdef    body_waiter
        xdef    body_waker
        xdef    body_sampler
        xdef    body_caller2
        xdef    body_regs
        xdef    body_overflow

; forever { counter++; fn(); }                     fn = task_yield
body_yielder:
        move.l  4(sp),a2
.loop:  addq.l  #1,(a2)
        move.l  4(a2),a0
        jsr     (a0)
        bra.s   .loop

; forever { counter++; }                           only preemption stops it
body_spinner:
        move.l  4(sp),a2
.loop:  addq.l  #1,(a2)
        bra.s   .loop

; The same at interrupt mask 1. The tick (level 3) still interrupts it, but
; that interrupt is not returning to task level, so it must not switch.
body_masked_spinner:
        move.l  4(sp),a2
        move.w  #$2100,sr
.loop:  addq.l  #1,(a2)
        bra.s   .loop

; forever { counter++; fn(param); }                fn = task_sleep
body_sleeper:
        move.l  4(sp),a2
.loop:  addq.l  #1,(a2)
        move.l  8(a2),-(sp)
        move.l  4(a2),a0
        jsr     (a0)
        addq.l  #4,sp
        bra.s   .loop

; counter++; return                                returning is exiting
body_once:
        move.l  4(sp),a0
        addq.l  #1,(a0)
        rts

; forever { fn(param); counter++; }                fn = task_wait, param = queue
body_waiter:
        move.l  4(sp),a2
.loop:  move.l  8(a2),-(sp)
        move.l  4(a2),a0
        jsr     (a0)
        addq.l  #4,sp
        addq.l  #1,(a2)
        bra.s   .loop

; forever { fn(param); fn2(aux); counter++; }      sleep, then wake a queue
body_waker:
        move.l  4(sp),a2
.loop:  move.l  8(a2),-(sp)
        move.l  4(a2),a0
        jsr     (a0)
        addq.l  #4,sp
        move.l  12(a2),-(sp)
        move.l  16(a2),a0
        jsr     (a0)
        addq.l  #4,sp
        addq.l  #1,(a2)
        bra.s   .loop

; forever { fn(param); result = fn2(aux); counter++; }
; A probe that runs inside the machine: sleep, then ask the kernel something
; and leave the answer where the test can read it. The test cannot simply
; call in from outside once the scheduler is running - an idle machine is
; sitting in STOP, and it would be calling on somebody else's context.
body_sampler:
        move.l  4(sp),a2
.loop:  move.l  8(a2),-(sp)
        move.l  4(a2),a0
        jsr     (a0)
        addq.l  #4,sp
        move.l  12(a2),-(sp)
        move.l  16(a2),a0
        jsr     (a0)
        addq.l  #4,sp
        move.l  d0,20(a2)
        addq.l  #1,(a2)
        bra.s   .loop

; forever { fn(param, aux); counter++; }         e.g. kprintf(level, fmt)
body_caller2:
        move.l  4(sp),a2
.loop:  move.l  12(a2),-(sp)
        move.l  8(a2),-(sp)
        move.l  4(a2),a0
        jsr     (a0)
        addq.l  #8,sp
        addq.l  #1,(a2)
        bra.s   .loop

; Load every register with a value derived from param, then spin checking
; them. Preemption lands wherever it lands; if any register ever differs,
; aux is set and stays set. Two of these with different params catch a
; switch that restores the wrong task's registers, not just a lost one.
body_regs:
        move.l  4(sp),a6
        move.l  8(a6),d7
        move.l  d7,a0
        move.l  a0,usp                  ; USP is per-task state too
        move.l  d7,d0
        addq.l  #1,d0
        move.l  d7,d1
        addq.l  #2,d1
        move.l  d7,d2
        addq.l  #3,d2
        move.l  d7,d3
        addq.l  #4,d3
        move.l  d7,d4
        addq.l  #5,d4
        move.l  d7,d5
        addq.l  #6,d5
        move.l  d7,d6
        addq.l  #7,d6
        lea     $10(a6),a0
        lea     $20(a6),a1
        lea     $30(a6),a2
        lea     $40(a6),a3
        lea     $50(a6),a4
        lea     $60(a6),a5
.loop:  addq.l  #1,(a6)
        cmp.l   8(a6),d7
        bne.s   .bad
        move.l  a1,d5                   ; borrow D5 to look at USP
        move.l  usp,a1
        cmp.l   a1,d7
        bne.s   .bad
        move.l  d5,a1
        sub.l   d7,d0
        subq.l  #1,d0
        bne.s   .bad
        move.l  d7,d0
        addq.l  #1,d0
        sub.l   d7,d1
        subq.l  #2,d1
        bne.s   .bad
        move.l  d7,d1
        addq.l  #2,d1
        sub.l   d7,d6
        subq.l  #7,d6
        bne.s   .bad
        move.l  d7,d6
        addq.l  #7,d6
        sub.l   a6,a0
        cmp.w   #$10,a0
        bne.s   .bad
        lea     $10(a6),a0
        sub.l   a6,a1
        cmp.w   #$20,a1
        bne.s   .bad
        lea     $20(a6),a1
        sub.l   a6,a5
        cmp.w   #$60,a5
        bne.s   .bad
        lea     $60(a6),a5
        bra.s   .loop
.bad:   move.l  #1,12(a6)
        bra.s   .loop

; Push param longwords, then fn(). More than the stack holds, and the yield
; is where the kernel gets to notice.          fn = task_yield
body_overflow:
        move.l  4(sp),a2
        move.l  8(a2),d2
.push:  clr.l   -(sp)
        subq.l  #1,d2
        bne.s   .push
        addq.l  #1,(a2)
        move.l  4(a2),a0
        jsr     (a0)
.hang:  bra.s   .hang
