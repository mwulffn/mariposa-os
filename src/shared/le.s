; ============================================================
; le.s - little-endian fields on a big-endian CPU
; ============================================================
; unsigned long le16_get(const void *p);      unsigned long le32_get(const void *p);
; void le16_put(void *p, unsigned long v);    void le32_put(void *p, unsigned long v);
;
; ext2 is little-endian throughout and the 68000 is not. Picking fields
; apart a byte at a time in C cost about 250 cycles a field - each helper a
; real call that called two more - and a block allocation touches a dozen
; fields: the profiler had these four at 2.5M cycles of a 512KB write, more
; than the filesystem logic around them. Load the field whole and swap.
;
; p MUST BE EVEN. Every ext2 structure is naturally aligned inside buffers
; that are long aligned, so every field is. FAT's are not - the BPB has
; fields at odd offsets - which is why fat16.c does not use these.
; ============================================================

        section .text

        xdef    _le16_get
        xdef    _le32_get
        xdef    _le16_put
        xdef    _le32_put

_le16_get:
        move.l  4(sp),a0
        moveq   #0,d0
        move.w  (a0),d0
        rol.w   #8,d0
        rts

_le32_get:
        move.l  4(sp),a0
        move.l  (a0),d0
        rol.w   #8,d0
        swap    d0
        rol.w   #8,d0
        rts

_le16_put:
        move.l  4(sp),a0
        move.l  8(sp),d0
        rol.w   #8,d0
        move.w  d0,(a0)
        rts

_le32_put:
        move.l  4(sp),a0
        move.l  8(sp),d0
        rol.w   #8,d0
        swap    d0
        rol.w   #8,d0
        move.l  d0,(a0)
        rts
