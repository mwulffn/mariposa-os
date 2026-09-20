; crt0.s - Kernel startup stub
; Receives control from ROM with:
;   A0 = struct bootinfo * (src/shared/bootinfo.h)
;   A1 = ROM panic entry point - also in the struct, but passed bare so
;        there is a way to complain when the struct is unreadable
;   A7 = top of fast RAM (stack)
;   SR = $2700 (supervisor, interrupts disabled)

        section .text

        xdef    _start
        xdef    _rom_panic
        xdef    _stack_top

        xref    _kernel_main
        xref    _ser_flush
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
        move.l  a0,-(sp)                ; save bootinfo pointer
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

        ; call kernel_main(bootinfo)
        move.l  (sp)+,a0                ; restore bootinfo pointer
        move.l  a0,-(sp)                ; push as C argument
        jsr     _kernel_main
        addq.l  #4,sp                   ; clean up argument

        ; kernel_main should never return
        ; if it does, panic - after getting queued output onto the wire,
        ; since the ROM bangs the UART and knows nothing of the ring
        jsr     _ser_flush
        move.l  _rom_panic,a0
        jmp     (a0)


        section .bss

_rom_panic:
        ds.l    1

; Top of the kernel stack, as handed over in A7. Reported at boot so that a
; stack landing somewhere it should not is visible rather than silent.
_stack_top:
        ds.l    1
