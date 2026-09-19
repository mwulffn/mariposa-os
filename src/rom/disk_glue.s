; ============================================================
; disk_glue.s - register ABI shims for disk.c
; ============================================================
; bootstrap.s calls these with values in D0-D2 and expects the documented
; registers back. The C behind them returns extra values through pointers,
; so each shim lends a few words of stack for the out-parameters.
; ============================================================

    xref    _rom_find_rdb
    xref    _rom_load_partition
    xref    _rom_fat16_init
    xref    _rom_fat16_find_file
    xref    _rom_fat16_read_cluster
    xref    _rom_fat16_next_cluster
    xref    _rom_load_system_bin

; ------------------------------------------------------------
; find_rdb - scan blocks 0-15. D0.l = 0 found, -1 not.
; The matching block is left in RDB_BUFFER.
; ------------------------------------------------------------
find_rdb:
    movem.l d1-d7/a0-a6,-(sp)
    jsr     _rom_find_rdb
    movem.l (sp)+,d1-d7/a0-a6
    rts

; ------------------------------------------------------------
; load_partition - D0.l = 0 ok, D1.l = start LBA, D2.l = sectors
; ------------------------------------------------------------
load_partition:
    movem.l d3-d7/a0-a6,-(sp)
    subq.l  #8,sp                   ; start_lba at 0(sp), sectors at 4(sp)
    move.l  sp,a0
    pea     4(a0)                   ; &sectors
    move.l  a0,-(sp)                ; &start_lba
    jsr     _rom_load_partition
    addq.l  #8,sp
    move.l  (sp)+,d1                ; start LBA
    move.l  (sp)+,d2                ; sectors
    movem.l (sp)+,d3-d7/a0-a6
    rts

; ------------------------------------------------------------
; fat16_init - D1.l = partition LBA. D0.l = 0 ok, -1 error.
; ------------------------------------------------------------
fat16_init:
    movem.l d1-d7/a0-a6,-(sp)
    move.l  d1,-(sp)
    jsr     _rom_fat16_init
    addq.l  #4,sp
    movem.l (sp)+,d1-d7/a0-a6
    rts

; ------------------------------------------------------------
; fat16_find_file - A0 = 11-byte name
; D0.l = 0 ok, D1.l = first cluster, D2.l = size
; ------------------------------------------------------------
fat16_find_file:
    movem.l d3-d7/a0-a6,-(sp)
    move.l  a0,a2                   ; name, across the stack juggling
    subq.l  #8,sp                   ; cluster at 0(sp), size at 4(sp)
    move.l  sp,a1
    pea     4(a1)                   ; &size
    move.l  a1,-(sp)                ; &cluster
    move.l  a2,-(sp)                ; name
    jsr     _rom_fat16_find_file
    lea     12(sp),sp
    move.l  (sp)+,d1                ; cluster
    move.l  (sp)+,d2                ; size
    movem.l (sp)+,d3-d7/a0-a6
    rts

; ------------------------------------------------------------
; fat16_read_cluster - A0 = destination, D0.l = cluster
; ------------------------------------------------------------
fat16_read_cluster:
    movem.l d1-d7/a0-a6,-(sp)
    move.l  d0,-(sp)                ; cluster
    move.l  a0,-(sp)                ; destination
    jsr     _rom_fat16_read_cluster
    addq.l  #8,sp
    movem.l (sp)+,d1-d7/a0-a6
    rts

; ------------------------------------------------------------
; fat16_get_next_cluster - D0.l = cluster in, next out (-1 on error)
; ------------------------------------------------------------
fat16_get_next_cluster:
    movem.l d1-d7/a0-a6,-(sp)
    move.l  d0,-(sp)
    jsr     _rom_fat16_next_cluster
    addq.l  #4,sp
    movem.l (sp)+,d1-d7/a0-a6
    rts

; ------------------------------------------------------------
; load_system_bin - D1.l = partition LBA, D2.l = partition size
; D0.l = 0 ok, D1.l = file size
; ------------------------------------------------------------
load_system_bin:
    movem.l d2-d7/a0-a6,-(sp)
    move.l  d1,d2                   ; partition LBA, across the stack juggling
    subq.l  #4,sp                   ; file size at 0(sp)
    move.l  sp,a0
    move.l  a0,-(sp)                ; &file_size
    move.l  d2,-(sp)                ; partition LBA
    jsr     _rom_load_system_bin
    addq.l  #8,sp
    move.l  (sp)+,d1                ; file size
    movem.l (sp)+,d2-d7/a0-a6
    rts
