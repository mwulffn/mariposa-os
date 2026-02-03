; ============================================================
; bootstrap.s - Main ROM entry point
; ============================================================
; Assembles with VASM: vasmm68k_mot -Fbin -I src/rom -o kick.rom bootstrap.s
; ============================================================

    include "hardware.i"

; Panic handler is defined in panic.s which is included at the end

; ============================================================
; ROM header
; ============================================================
    org $FC0000

rom_start:
    dc.l    $3FFC               ; Offset 0: Initial SSP (temporary boot stack)
    dc.l    start               ; Offset 4: Initial PC
    dc.l    ROM_MAGIC           ; Offset 8: Magic 'AMAG'
    dc.w    ROM_VERSION         ; Offset 12: Version 0.1
    dc.w    ROM_FLAGS           ; Offset 14: Flags

; ============================================================
; Entry point
; ============================================================
start:
    ; ============================================================
    ; 1. HARDWARE INIT
    ; ============================================================
    ; Disable interrupts at CPU level
    move.w  #$2700,sr

    ; Point to custom chips
    lea     CUSTOM,a6

    ; Disable all DMA and interrupts
    move.w  #$7FFF,DMACON(a6)
    move.w  #$7FFF,INTENA(a6)
    move.w  #$7FFF,INTREQ(a6)

    ; Disable ROM overlay so chip RAM is visible at $0
    move.b  #$03,CIAA_DDRA
    move.b  #$00,CIAA_PRA

    ; Initialize CIAs to known state
    move.b  #$00,CIAA_CRA       ; Stop Timer A
    move.b  #$00,CIAA_CRB       ; Stop Timer B
    move.b  #$7F,CIAA_ICR       ; Clear all CIA-A interrupt enables
    move.b  #$00,CIAB_CRA       ; Stop Timer A
    move.b  #$00,CIAB_CRB       ; Stop Timer B
    move.b  #$7F,CIAB_ICR       ; Clear all CIA-B interrupt enables

    ; ============================================================
    ; 2. EXCEPTION VECTOR INSTALLATION
    ; ============================================================
    bsr     install_exception_vectors

    ; ============================================================
    ; 3. MEMORY DETECTION
    ; ============================================================
    bsr     configure_zorro_ii  ; Must be first (for fast RAM)
    bsr     build_memory_table  ; Detects, tests, builds table

    ; ============================================================
    ; 4. SERIAL INIT
    ; ============================================================
    bsr     serial_init

    ; Print version banner
    lea     banner_msg(pc),a0
    bsr     serial_put_string

    ; Print memory map table
    bsr     print_memory_map

    ; ============================================================
    ; 5. SUCCESS HALT - BRIGHT GREEN SCREEN
    ; ============================================================
    ; Set up display
    bsr     init_display

    ; Set COLOR00 to bright green
    move.w  #$0F0,COLOR00(a6)

    ; Print success message to serial
    lea     success_msg(pc),a0
    bsr     serial_put_string

    ; ============================================================
    ; 6. RDB DETECTION
    ; ============================================================
    bsr     find_rdb
    tst.l   d0
    bne     .enter_debugger         ; Skip partition if RDB not found

    ; ============================================================
    ; 7. PARTITION LOADING
    ; ============================================================
    bsr     load_partition
    tst.l   d0
    bne     .enter_debugger         ; Go to debugger if error
    ; On success: D1 = partition start LBA, D2 = partition size in blocks

    ; ============================================================
    ; 8. LOAD SYSTEM.BIN FROM PARTITION
    ; ============================================================
    ; D1 = partition start LBA, D2 = partition size
    bsr     load_system_bin
    tst.l   d0
    bne     .enter_debugger         ; Go to debugger if error
    ; On success: D1 = file size, kernel loaded at $200000

    ; ============================================================
    ; 9. JUMP TO KERNEL
    ; ============================================================
    pea     boot_kernel_msg(pc)
    bsr     SerialPrintf
    addq.l  #4,sp

    ; Find kernel stack (RESERVED entry) in memory table
    lea     MEMMAP_TABLE,a2
.find_stack:
    move.l  (a2)+,d2            ; base
    move.l  (a2)+,d3            ; size
    move.w  (a2)+,d4            ; type
    addq.l  #2,a2               ; skip flags

    ; Check for end of table (both base AND size are 0)
    move.l  d2,d0
    or.l    d3,d0
    beq.s   .no_stack_found

    ; Look for RESERVED type
    cmp.w   #MEM_TYPE_RESERVED,d4
    bne.s   .find_stack

    ; Check if this is in fast RAM range ($200000+)
    cmp.l   #$200000,d2
    blt.s   .find_stack         ; Skip low reserved areas

    ; Kernel stack found: stack = base + size (top)
    add.l   d2,d3

    ; Set kernel entry parameters:
    ; A0 = memory map pointer
    lea     MEMMAP_TABLE,a0
    ; A1 = ROM panic handler
    lea     debugger_entry(pc),a1
    ; SR = supervisor, interrupts disabled
    move.w  #$2700,sr
    ; SP = top of kernel stack
    move.l  d3,sp
    ; Jump to kernel
    jmp     KERNEL_LOAD_ADDR

.no_stack_found:
    pea     no_kernel_stack_msg(pc)
    bsr     SerialPrintf
    addq.l  #4,sp

.enter_debugger:
    ; Enter interactive debugger
    jmp     debugger_entry

; ============================================================
; install_exception_vectors - Install all exception handlers
; ============================================================
install_exception_vectors:
    movem.l a0-a1,-(sp)

    ; Install specific exception handlers
    move.l  #bus_error_handler,VEC_BUS_ERROR
    move.l  #address_error_handler,VEC_ADDR_ERROR
    move.l  #illegal_handler,VEC_ILLEGAL
    move.l  #zero_divide_handler,VEC_ZERO_DIV
    move.l  #chk_handler,VEC_CHK
    move.l  #trapv_handler,VEC_TRAPV
    move.l  #privilege_handler,VEC_PRIV_VIOL
    move.l  #trace_handler,VEC_TRACE
    move.l  #line_a_handler,VEC_LINE_A
    move.l  #line_f_handler,VEC_LINE_F
    move.l  #spurious_handler,VEC_SPURIOUS

    ; Install autovector handlers (IRQ 1-7)
    move.l  #auto_vec_handler,VEC_AUTOVEC1
    move.l  #auto_vec_handler,VEC_AUTOVEC2
    move.l  #auto_vec_handler,VEC_AUTOVEC3
    move.l  #auto_vec_handler,VEC_AUTOVEC4
    move.l  #auto_vec_handler,VEC_AUTOVEC5
    move.l  #auto_vec_handler,VEC_AUTOVEC6
    move.l  #auto_vec_handler,VEC_AUTOVEC7

    ; Install TRAP handlers (0-15)
    movem.l d0,-(sp)
    lea     VEC_TRAP0,a0
    moveq   #15,d0
.trap_loop:
    move.l  #trap_handler,(a0)+
    dbf     d0,.trap_loop

    ; Install generic handler for remaining vectors
    lea     $0030,a0            ; Start after reserved vectors
    move.w  #(256-12-1),d0      ; Remaining vectors
.generic_loop:
    move.l  #generic_handler,(a0)+
    dbf     d0,.generic_loop

    movem.l (sp)+,d0

    movem.l (sp)+,a0-a1
    rts

; ============================================================
; Exception Handlers
; ============================================================
bus_error_handler:
    lea     bus_error_msg(pc),a0
    jmp     panic_with_msg

address_error_handler:
    lea     addr_error_msg(pc),a0
    jmp     panic_with_msg

illegal_handler:
    lea     illegal_msg(pc),a0
    jmp     panic_with_msg

zero_divide_handler:
    lea     zero_div_msg(pc),a0
    jmp     panic_with_msg

chk_handler:
    lea     chk_msg(pc),a0
    jmp     panic_with_msg

trapv_handler:
    lea     trapv_msg(pc),a0
    jmp     panic_with_msg

privilege_handler:
    lea     priv_msg(pc),a0
    jmp     panic_with_msg

trace_handler:
    lea     trace_msg(pc),a0
    jmp     panic_with_msg

line_a_handler:
    lea     line_a_msg(pc),a0
    jmp     panic_with_msg

line_f_handler:
    lea     line_f_msg(pc),a0
    jmp     panic_with_msg

spurious_handler:
    lea     spurious_msg(pc),a0
    jmp     panic_with_msg

auto_vec_handler:
    lea     auto_vec_msg(pc),a0
    jmp     panic_with_msg

trap_handler:
    lea     trap_msg(pc),a0
    jmp     panic_with_msg

generic_handler:
    lea     generic_exc_msg(pc),a0
    jmp     panic_with_msg

; ============================================================
; init_display - Set up basic display for green screen
; ============================================================
init_display:
    movem.l d0/a0/a6,-(sp)
    lea     CUSTOM,a6

    ; Clear screen memory
    lea     SCREEN,a0
    move.w  #((SCREEN_HEIGHT*BYTES_PER_ROW)/4)-1,d0
.clrloop:
    clr.l   (a0)+
    dbf     d0,.clrloop

    ; Set up display
    move.w  #$1200,BPLCON0(a6)      ; 1 bitplane, color on
    move.w  #$0000,BPLCON1(a6)
    move.w  #$0000,BPLCON2(a6)
    move.w  #$0000,BPL1MOD(a6)
    move.w  #$0000,BPL2MOD(a6)

    ; PAL display window
    move.w  #$2C81,DIWSTRT(a6)
    move.w  #$2CC1,DIWSTOP(a6)
    move.w  #$0038,DDFSTRT(a6)
    move.w  #$00D0,DDFSTOP(a6)

    ; Set bitplane pointer
    move.l  #SCREEN,d0
    move.w  d0,BPL1PTL(a6)
    swap    d0
    move.w  d0,BPL1PTH(a6)

    ; Set up copper list
    bsr     init_copper

    ; Start copper
    move.l  #COPPERLIST,d0
    move.w  d0,COP1LCL(a6)
    swap    d0
    move.w  d0,COP1LCH(a6)
    move.w  COPJMP1(a6),d0

    ; Enable DMA
    move.w  #$8380,DMACON(a6)       ; SET + BPLEN + COPEN + DMAEN

    movem.l (sp)+,d0/a0/a6
    rts

; ============================================================
; init_copper - Build copper list
; ============================================================
init_copper:
    movem.l a0,-(sp)
    lea     COPPERLIST,a0

    move.w  #BPL1PTH,(a0)+
    move.w  #(SCREEN>>16)&$FFFF,(a0)+
    move.w  #BPL1PTL,(a0)+
    move.w  #SCREEN&$FFFF,(a0)+

    move.l  #$FFFFFFFE,(a0)+        ; End

    movem.l (sp)+,a0
    rts

; ============================================================
; Data
; ============================================================
banner_msg:
    dc.b    "AMAG ROM v0.1",10,13,0
    even

newline_msg:
    dc.b    10,13,0
    even

success_msg:
    dc.b    "Boot success - GREEN SCREEN",10,13,0
    even

boot_kernel_msg:
    dc.b    "Jumping to kernel at $200000...",10,13,0
    even

no_kernel_stack_msg:
    dc.b    "ERROR: No kernel stack in memory table!",10,13,0
    even

bus_error_msg:
    dc.b    "BUS ERROR",0
    even

addr_error_msg:
    dc.b    "ADDRESS ERROR",0
    even

illegal_msg:
    dc.b    "ILLEGAL INSTRUCTION",0
    even

zero_div_msg:
    dc.b    "DIVIDE BY ZERO",0
    even

chk_msg:
    dc.b    "CHK EXCEPTION",0
    even

trapv_msg:
    dc.b    "TRAPV EXCEPTION",0
    even

priv_msg:
    dc.b    "PRIVILEGE VIOLATION",0
    even

trace_msg:
    dc.b    "TRACE EXCEPTION",0
    even

line_a_msg:
    dc.b    "LINE-A EXCEPTION",0
    even

line_f_msg:
    dc.b    "LINE-F EXCEPTION",0
    even

spurious_msg:
    dc.b    "SPURIOUS INTERRUPT",0
    even

auto_vec_msg:
    dc.b    "AUTOVECTOR INTERRUPT",0
    even

trap_msg:
    dc.b    "TRAP EXCEPTION",0
    even

generic_exc_msg:
    dc.b    "UNKNOWN EXCEPTION",0
    even

; ============================================================
; Include panic, autoconfig, memory, serial, sprintf, and debugger modules
; ============================================================
    include "panic.s"
    include "autoconfig.s"
    include "memory.s"
    include "serial.s"
    include "sprintf.s"
    include "debugger.s"
    include "ide.s"
    include "partition.s"
    include "filesystem.s"

; ============================================================
; ROM footer - pad to 256KB and add checksum location
; ============================================================
    org $FFFFFC
rom_end:
    dc.l    rom_start

    end
