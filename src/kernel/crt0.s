; crt0.s - Kernel startup stub
; Receives control from ROM with:
;   A0 = memory map pointer
;   A1 = ROM panic entry point
;   A7 = top of fast RAM (stack)
;   SR = $2700 (supervisor, interrupts disabled)

        section .text

        xdef    _start
        xdef    _rom_panic
        xdef    _stack_top

        xref    _kernel_main
        xref    __bss_start
        xref    __bss_end

_start:
        ; A7 on entry is the top of the stack the ROM picked for us, and it
        ; is the only record of it: nothing else in the kernel can work out
        ; where the stack ends. Grab it before the first push moves it.
        move.l  sp,d0

        ; stash ROM parameters before we clobber registers. All three go on
        ; the stack, not into .bss - _rom_panic and _stack_top live in .bss
        ; and the clear below would zero them straight back out again.
        move.l  a0,-(sp)                ; save memmap pointer
        move.l  a1,-(sp)                ; save panic function
        move.l  d0,-(sp)                ; save entry stack pointer

        ; clear .bss
        lea     __bss_start,a2
        lea     __bss_end,a3
.clrbss:
        cmp.l   a2,a3
        beq.s   .bss_done
        clr.b   (a2)+
        bra.s   .clrbss
.bss_done:

        ; .bss is zeroed, so the saved values can be published now
        move.l  (sp)+,d0                ; restore entry stack pointer
        move.l  d0,_stack_top
        move.l  (sp)+,a1                ; restore panic function
        move.l  a1,_rom_panic

        ; call kernel_main(memmap)
        move.l  (sp)+,a0                ; restore memmap pointer
        move.l  a0,-(sp)                ; push as C argument
        jsr     _kernel_main
        addq.l  #4,sp                   ; clean up argument

        ; kernel_main should never return
        ; if it does, panic
        move.l  _rom_panic,a0
        jmp     (a0)


        section .bss

_rom_panic:
        ds.l    1

; Top of the kernel stack, as handed over in A7. Reported at boot so that a
; stack landing somewhere it should not is visible rather than silent.
_stack_top:
        ds.l    1
