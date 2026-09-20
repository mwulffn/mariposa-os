; ============================================================
; memory_glue.s - register ABI shims for memory.c
; ============================================================

    xref    _rom_detect_chip_ram
    xref    _rom_detect_fast_ram
    xref    _rom_detect_slow_ram
    xref    _rom_build_memory_table
    xref    _rom_print_memory_map
    xref    _rom_reserve_kernel_image
    xref    _rom_kernel_stack_top

; detect_chip_ram - D0.l = chip RAM size in bytes
detect_chip_ram:
    movem.l d1/a0-a1,-(sp)
    jsr     _rom_detect_chip_ram
    movem.l (sp)+,d1/a0-a1
    rts

; detect_fast_ram - D0.l = fast RAM size in bytes, 0 if none
detect_fast_ram:
    movem.l d1/a0-a1,-(sp)
    jsr     _rom_detect_fast_ram
    movem.l (sp)+,d1/a0-a1
    rts

; detect_slow_ram - D0.l = trapdoor RAM size at $C00000, 0 if none
detect_slow_ram:
    movem.l d1/a0-a1,-(sp)
    jsr     _rom_detect_slow_ram
    movem.l (sp)+,d1/a0-a1
    rts

; build_memory_table - fills MEMMAP_TABLE. Halts on a chip RAM failure.
build_memory_table:
    movem.l d0-d7/a0-a6,-(sp)
    jsr     _rom_build_memory_table
    movem.l (sp)+,d0-d7/a0-a6
    rts

; print_memory_map - writes the table to the serial port
print_memory_map:
    movem.l d0-d7/a0-a6,-(sp)
    jsr     _rom_print_memory_map
    movem.l (sp)+,d0-d7/a0-a6
    rts

; kernel_stack_top - D0.l = top of the kernel stack, 0 if the table has none
kernel_stack_top:
    movem.l d1/a0-a1,-(sp)
    jsr     _rom_kernel_stack_top
    movem.l (sp)+,d1/a0-a1
    rts

; reserve_kernel_image - D0.l = image size in bytes
; Marks the loaded kernel reserved so it is not handed out as free fast RAM.
; Returns D0.l = 0 on success, -1 if the table could not be split.
reserve_kernel_image:
    movem.l d1-d7/a0-a6,-(sp)
    move.l  d0,-(sp)
    jsr     _rom_reserve_kernel_image
    addq.l  #4,sp
    movem.l (sp)+,d1-d7/a0-a6
    rts
