; ============================================================
; cpu_detect.s - which 680x0 and which FPU
; ============================================================
; unsigned long rom_cpu_detect(void)
;   low word  = CPU_* from src/shared/bootinfo.h
;   high word = FPU_*
;
; Detection is by trying an instruction the next CPU up added and seeing
; whether it traps. The catch is that the exception frame - the thing a trap
; handler would normally unwind with RTE - is itself one of the differences
; being detected. So the handler does not unwind anything: the probe records
; its stack pointer and a bail-out address first, and the handler just
; restores the one and jumps to the other.
;
; The ROM is assembled -m68000, so the newer instructions are spelled dc.w.
;
; Probes, in order:
;   movec vbr,d0     traps on a 68000
;   extb.l d0        traps on a 68010. Not movec cacr, the obvious probe:
;                    it would be right on silicon, but Musashi's 68010 quietly
;                    ignores it, and a probe the tests cannot see fail is a
;                    probe that is not tested.
;   movec itt0,d0    works on a 68040/68060 only
;   movec pcr,d0     works on a 68060 only
;   CACR bit 9       sticks on a 68030 (freeze data cache), not on a 68020.
;                    Chosen because it turns nothing on; bit 31 would tell a
;                    68040 apart too, but it enables a data cache nobody has
;                    invalidated.
;   fnop             traps (line F) with no FPU
;
; Runs at SR $2700 with the vector table at 0: VBR resets to 0 and nothing
; has moved it yet. Preserves D2-D7/A2-A6 for vbcc.
; ============================================================

CPU_68000       equ 0
CPU_68010       equ 1
CPU_68020       equ 2
CPU_68030       equ 3
CPU_68040       equ 4
CPU_68060       equ 6

FPU_NONE        equ 0
FPU_6888X       equ 1
FPU_68040       equ 4
FPU_68060       equ 6

    xdef    _rom_cpu_detect

_rom_cpu_detect:
    movem.l d2/a2-a6,-(sp)

    move.l  VEC_ILLEGAL,a2          ; ours to put back
    move.l  VEC_LINE_F,a3
    lea     .trapped(pc),a0
    move.l  a0,VEC_ILLEGAL
    move.l  a0,VEC_LINE_F

    move.l  sp,a5                   ; what .trapped restores
    moveq   #CPU_68000,d2

    lea     .cpu_done(pc),a6
    dc.w    $4E7A,$0801             ; movec vbr,d0
    moveq   #CPU_68010,d2

    dc.w    $49C0                   ; extb.l d0
    moveq   #CPU_68020,d2

    lea     .not_040(pc),a6
    dc.w    $4E7A,$0004             ; movec itt0,d0
    moveq   #CPU_68040,d2

    lea     .cpu_done(pc),a6
    dc.w    $4E7A,$0808             ; movec pcr,d0
    moveq   #CPU_68060,d2
    bra.s   .cpu_done

.not_040:
    lea     .cpu_done(pc),a6
    move.l  #$0200,d0               ; FD: freeze data cache
    dc.w    $4E7B,$0002             ; movec d0,cacr
    dc.w    $4E7A,$0002             ; movec cacr,d0
    btst    #9,d0
    beq.s   .clear_cacr
    moveq   #CPU_68030,d2
.clear_cacr:
    moveq   #0,d0
    dc.w    $4E7B,$0002             ; movec d0,cacr

.cpu_done:
    ; FPU. D1 is built up as the answer and only kept if fnop survives.
    moveq   #FPU_NONE,d1
    cmp.w   #CPU_68020,d2
    blo.s   .fpu_done               ; no coprocessor interface before the 020
    lea     .fpu_done(pc),a6
    moveq   #FPU_6888X,d0
    cmp.w   #CPU_68040,d2
    blo.s   .try_fpu
    move.w  d2,d0                   ; on-chip: FPU_68040 / FPU_68060
.try_fpu:
    dc.w    $F280,$0000             ; fnop
    move.w  d0,d1

.fpu_done:
    move.l  a2,VEC_ILLEGAL
    move.l  a3,VEC_LINE_F

    swap    d1
    clr.w   d1
    move.l  d1,d0
    or.w    d2,d0

    movem.l (sp)+,d2/a2-a6
    rts

; Whatever frame the CPU built, drop it.
.trapped:
    move.l  a5,sp
    jmp     (a6)
