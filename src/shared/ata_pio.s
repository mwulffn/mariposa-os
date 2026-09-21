; ============================================================
; ata_pio.s - the PIO data loops, shared by the ROM and the kernel
; ============================================================
; void ata_pio_read(volatile unsigned short *port, void *buf, unsigned long words);
; void ata_pio_write(volatile unsigned short *port, const void *buf, unsigned long words);
;
; Gayle's IDE has no DMA: every word of every sector goes through one of
; these two loops, which makes them the hottest code in the system whenever
; the disk is in use. In C they cost about 48 cycles a word - the compiler
; keeps the loop counter and both pointers in memory-friendly but slow form.
; Here a word is one 12-cycle MOVE, and the loop overhead is paid once per
; sixteen. `words` is always a multiple of 256, so of sixteen.
;
; vbcc ABI: arguments on the stack, D0/D1/A0/A1 are scratch.
; ============================================================

        section .text

        xdef    _ata_pio_read
        xdef    _ata_pio_write

_ata_pio_read:
        move.l  4(sp),a0                ; the data port: not incremented
        move.l  8(sp),a1
        move.l  12(sp),d0
        lsr.l   #4,d0
        beq.s   .rdone
        subq.w  #1,d0
.rloop: move.w  (a0),(a1)+
        move.w  (a0),(a1)+
        move.w  (a0),(a1)+
        move.w  (a0),(a1)+
        move.w  (a0),(a1)+
        move.w  (a0),(a1)+
        move.w  (a0),(a1)+
        move.w  (a0),(a1)+
        move.w  (a0),(a1)+
        move.w  (a0),(a1)+
        move.w  (a0),(a1)+
        move.w  (a0),(a1)+
        move.w  (a0),(a1)+
        move.w  (a0),(a1)+
        move.w  (a0),(a1)+
        move.w  (a0),(a1)+
        dbf     d0,.rloop
.rdone: rts

_ata_pio_write:
        move.l  4(sp),a0
        move.l  8(sp),a1
        move.l  12(sp),d0
        lsr.l   #4,d0
        beq.s   .wdone
        subq.w  #1,d0
.wloop: move.w  (a1)+,(a0)
        move.w  (a1)+,(a0)
        move.w  (a1)+,(a0)
        move.w  (a1)+,(a0)
        move.w  (a1)+,(a0)
        move.w  (a1)+,(a0)
        move.w  (a1)+,(a0)
        move.w  (a1)+,(a0)
        move.w  (a1)+,(a0)
        move.w  (a1)+,(a0)
        move.w  (a1)+,(a0)
        move.w  (a1)+,(a0)
        move.w  (a1)+,(a0)
        move.w  (a1)+,(a0)
        move.w  (a1)+,(a0)
        move.w  (a1)+,(a0)
        dbf     d0,.wloop
.wdone: rts
