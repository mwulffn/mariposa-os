; crt0.s - Kernel startup stub
; Receives control from ROM with:
;   A0 = memory map pointer
;   A1 = ROM panic entry point
;   A7 = top of fast RAM (stack)
;   SR = $2700 (supervisor, interrupts disabled)

        section .text

        xdef    _start
        xdef    _rom_panic

        xref    _kernel_main
        xref    __bss_start
        xref    __bss_end

_start:
        ; save ROM parameters before we clobber registers
        move.l  a0,-(sp)                ; save memmap pointer
        move.l  a1,_rom_panic           ; save panic function

        ; clear .bss
        lea     __bss_start,a2
        lea     __bss_end,a3
.clrbss:
        cmp.l   a2,a3
        beq.s   .bss_done
        clr.b   (a2)+
        bra.s   .clrbss
.bss_done:

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
