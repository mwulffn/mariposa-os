; ============================================================
; cpu.s - interrupt control primitives
; ============================================================
; Per docs/interrupt_control_design.md. On the 68000 a write to SR is a
; single instruction and inherently atomic, so these are thin wrappers and
; the atomicity comes for free.
;
; The C prototypes in cpu.h all use unsigned long rather than a 16-bit SR,
; so the argument occupies one unambiguous longword slot on the stack. A
; short would leave it open to how vbcc chooses to promote it.
;
; Position independent - no data, no absolute references - so the tests can
; assemble this standalone to origin zero and load it anywhere, the same way
; libsup.s is handled.
; ============================================================

        section .text

        xdef    _cpu_sr_get
        xdef    _cpu_sr_set
        xdef    _cpu_int_disable
        xdef    _cpu_int_enable
        xdef    _cpu_idle

; unsigned long cpu_sr_get(void) - current SR, zero extended
;
; Read SR before touching D0, not after. Clearing the register first is the
; obvious way to write this and it is wrong: moveq sets the Z flag, and the
; flags ARE the low byte of SR, so the read comes back with the condition
; codes this function just invented. Caught by cpu.sr_get, which saw $2704
; where it wanted $2700.
_cpu_sr_get:
        move.w  sr,d0                   ; low word only; high word is stale
        and.l   #$FFFF,d0               ; safe now - SR is already captured
        rts

; void cpu_sr_set(unsigned long sr)
_cpu_sr_set:
        move.l  4(sp),d0
        move.w  d0,sr
        rts

; unsigned long cpu_int_disable(void) - mask all levels, return the OLD SR
;
; The design doc describes this as "writes $2700 to SR". It returns the
; previous SR as well, because rule 1 in that same doc is never to use blind
; enable/disable pairs - and a caller cannot restore what it was never told.
; That is what makes CRITICAL_ENTER/CRITICAL_EXIT in cpu.h possible.
_cpu_int_disable:
        move.w  sr,d0                   ; read first - see cpu_sr_get above
        and.l   #$FFFF,d0
        move.w  #$2700,sr
        rts

; void cpu_int_enable(void) - supervisor, all levels unmasked
;
; Blunt on purpose: this is the one-way switch that opens the CPU gate at
; startup. Anything nested must save and restore instead - see cpu.h.
_cpu_int_enable:
        move.w  #$2000,sr
        rts

; void cpu_idle(void) - stop the CPU until the next interrupt
;
; STOP loads SR and halts in one instruction, which is the whole point: a
; separate "enable, then halt" has a window where the interrupt arrives
; between the two and the halt then sleeps through the event it was waiting
; for. It lowers the mask to 0, so this is for the idle loop and for code
; that is waiting on an interrupt - never inside a critical section.
_cpu_idle:
        stop    #$2000
        rts
