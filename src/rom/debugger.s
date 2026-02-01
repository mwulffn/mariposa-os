; ============================================================
; debugger.s - Interactive debugger for Amiga ROM
; ============================================================
; Commands:
;   r              - Display all registers
;   r <reg> <hex>  - Set register (D0-D7, A0-A7, PC, SR)
;   m[.b] <addr>   - Memory dump as bytes (16 bytes)
;   m.w <addr>     - Memory dump as words (8 words)
;   m.l <addr>     - Memory dump as longs (4 longwords)
;   m <addr> <hex> - Memory write (auto-sizes: 1-2=byte, 3-4=word, 5-8=long)
;   g              - Continue execution
;   g <addr>       - Continue from address
;   ?              - Help
; ============================================================

; ============================================================
; DebuggerEntry - Entry point from boot
; ============================================================
; Saves all registers and enters interactive debugger
DebuggerEntry:
    ; Save registers to dump area
    movem.l d0-d7/a0-a6,saved_regs
    move.l  sp,saved_a7
    move.w  sr,saved_sr
    move.l  #DebuggerEntry,saved_pc      ; Boot entry, not a crash

    ; Fall through to DebuggerMain

; ============================================================
; DebuggerMain - Main debugger loop
; ============================================================
; Displays prompt, reads commands, dispatches to handlers
DebuggerMain:
    ; Initialize debugger state
    clr.l   DBG_BUF_IDX
    clr.l   DBG_LAST_ADDR

    ; Set up display (already done by bootstrap, but safe to repeat)
    bsr     init_display

    ; Print banner
    lea     .banner(pc),a0
    bsr     SerialPutString

    ; Print prompt and enter command loop
.cmd_loop:
    lea     .prompt(pc),a0
    bsr     SerialPutString

    ; Read command line
    bsr     DbgReadLine

    ; Parse and dispatch
    bsr     ParseCommand

    ; Loop
    bra.s   .cmd_loop

.banner:
    dc.b    10,13,"AMAG Debugger v0.1",10,13
    dc.b    "Type '?' for help",10,13,0
.prompt:
    dc.b    10,13,"> ",0
    even

; ============================================================
; DbgReadLine - Read command line from serial
; ============================================================
; Reads until Enter, handles backspace, stores in DBG_CMD_BUF
; Returns with buffer null-terminated
DbgReadLine:
    movem.l d0-d2/a0,-(sp)

    lea     DBG_CMD_BUF,a0
    moveq   #0,d1                       ; Buffer index

.loop:
    bsr     SerialWaitChar              ; Get character in d0

    ; Check for Enter (CR or LF)
    cmp.b   #13,d0
    beq.s   .done
    cmp.b   #10,d0
    beq.s   .done

    ; Check for backspace
    cmp.b   #8,d0
    beq.s   .backspace
    cmp.b   #127,d0
    beq.s   .backspace

    ; Check buffer full (leave room for null)
    cmp.w   #127,d1
    bge.s   .loop

    ; Store character
    move.b  d0,(a0,d1.w)
    addq.w  #1,d1

    ; Echo character
    bsr     SerialPutChar
    bra.s   .loop

.backspace:
    ; Can't backspace past start
    tst.w   d1
    beq.s   .loop

    ; Remove character
    subq.w  #1,d1

    ; Echo backspace sequence: BS, space, BS
    move.b  #8,d0
    bsr     SerialPutChar
    move.b  #' ',d0
    bsr     SerialPutChar
    move.b  #8,d0
    bsr     SerialPutChar
    bra.s   .loop

.done:
    ; Null terminate
    clr.b   (a0,d1.w)

    movem.l (sp)+,d0-d2/a0
    rts

; ============================================================
; ParseCommand - Parse and dispatch command
; ============================================================
; Reads first character from DBG_CMD_BUF and dispatches
ParseCommand:
    movem.l d0/a0,-(sp)

    lea     DBG_CMD_BUF,a0
    move.b  (a0),d0

    ; Skip leading spaces
    bsr     SkipWhitespace
    move.b  (a0),d0

    ; Empty command - ignore
    tst.b   d0
    beq     .done

    ; Convert to uppercase
    cmp.b   #'a',d0
    blt.s   .check_cmd
    cmp.b   #'z',d0
    bgt.s   .check_cmd
    sub.b   #32,d0

.check_cmd:
    cmp.b   #'R',d0
    beq     .do_regs
    cmp.b   #'M',d0
    beq     .check_mem_mode         ; Check for m, m.w, m.l
    cmp.b   #'G',d0
    beq     .do_go
    cmp.b   #'?',d0
    beq     .do_help

    ; Unknown command
    lea     .unknown(pc),a0
    bsr     SerialPutString
    bra     .done

.check_mem_mode:
    ; Check for .w or .l suffix
    cmp.b   #'.',1(a0)              ; Check char after 'M'
    bne     .do_mem                 ; Plain 'm' - byte mode

    move.b  2(a0),d0                ; Get char after '.'
    ; Convert to uppercase
    cmp.b   #'a',d0
    blt.s   .check_w
    sub.b   #32,d0
.check_w:
    cmp.b   #'B',d0
    beq     .do_mem_byte            ; m.b = byte mode (same as m)
    cmp.b   #'W',d0
    beq     .do_mem_word
    cmp.b   #'L',d0
    beq     .do_mem_long
    bra     .do_mem                 ; Unknown suffix, default to byte

.do_mem_byte:
    moveq   #0,d0                   ; Mode 0 = byte
    bsr     CmdMemory
    bra     .done

.do_mem:
    moveq   #0,d0                   ; Mode 0 = byte
    bsr     CmdMemory
    bra     .done

.do_mem_word:
    moveq   #1,d0                   ; Mode 1 = word
    bsr     CmdMemory
    bra     .done

.do_mem_long:
    moveq   #2,d0                   ; Mode 2 = long
    bsr     CmdMemory
    bra     .done

.do_regs:
    bsr     CmdRegisters
    bra     .done

.do_go:
    bsr     CmdGo
    bra     .done

.do_help:
    bsr     CmdHelp

.done:
    movem.l (sp)+,d0/a0
    rts

.unknown:
    dc.b    "Unknown command (type ? for help)",0
    even

; ============================================================
; SkipWhitespace - Advance A0 past spaces/tabs
; ============================================================
; A0 = string pointer (modified)
SkipWhitespace:
    movem.l d0,-(sp)
.loop:
    move.b  (a0),d0
    cmp.b   #' ',d0
    beq.s   .skip
    cmp.b   #9,d0                       ; Tab
    beq.s   .skip
    bra.s   .done
.skip:
    addq.l  #1,a0
    bra.s   .loop
.done:
    movem.l (sp)+,d0
    rts

; ============================================================
; ParseHex - Parse hex string to D0.l
; ============================================================
; A0 = string pointer (advanced past hex digits)
; Returns D0.l = parsed value, D1.w = digit count, Z flag set if no digits found
ParseHex:
    movem.l d2-d3,-(sp)             ; Don't save D1 - used for return value

    moveq   #0,d0                       ; Result
    moveq   #0,d3                       ; Digit count

    ; Skip $ prefix if present
    cmp.b   #'$',(a0)
    bne.s   .parse_loop
    addq.l  #1,a0

.parse_loop:
    move.b  (a0),d1

    ; Convert to uppercase
    cmp.b   #'a',d1
    blt.s   .check_digit
    cmp.b   #'z',d1
    bgt.s   .check_digit
    sub.b   #32,d1

.check_digit:
    ; Check for hex digit
    moveq   #0,d2
    cmp.b   #'0',d1
    blt.s   .done
    cmp.b   #'9',d1
    ble.s   .is_digit
    cmp.b   #'A',d1
    blt.s   .done
    cmp.b   #'F',d1
    bgt.s   .done

    ; A-F
    sub.b   #'A'-10,d1
    bra.s   .add_digit

.is_digit:
    sub.b   #'0',d1

.add_digit:
    ; Shift result left 4 bits and add new digit
    lsl.l   #4,d0
    or.b    d1,d0
    addq.w  #1,d3
    addq.l  #1,a0
    bra.s   .parse_loop

.done:
    move.w  d3,d1                   ; Return digit count in D1
    ; Set Z flag based on digit count
    tst.w   d3

    movem.l (sp)+,d2-d3
    rts

; ============================================================
; CmdRegisters - Display or modify registers
; ============================================================
; Syntax: r             - Display all
;         r <reg> <val> - Set register
CmdRegisters:
    movem.l d0-d3/a0-a2,-(sp)

    lea     DBG_CMD_BUF,a0
    addq.l  #1,a0                       ; Skip 'r'
    bsr     SkipWhitespace

    ; Check if register name follows
    move.b  (a0),d0
    beq.w   .display_all                ; No args - display all

    ; Parse register name (2 chars)
    move.b  (a0)+,d1
    move.b  (a0)+,d2

    ; Convert to uppercase
    cmp.b   #'a',d1
    blt.s   .check_d
    sub.b   #32,d1
.check_d:

    ; Check for D0-D7
    cmp.b   #'D',d1
    bne.s   .check_a
    sub.b   #'0',d2
    cmp.b   #7,d2
    bgt.w   .bad_reg

    ; D register - get address
    ext.w   d2
    lsl.w   #2,d2                       ; *4 for longword offset
    lea     saved_d0,a1
    add.w   d2,a1
    bra.w   .parse_value

.check_a:
    ; Check for A0-A7
    cmp.b   #'A',d1
    bne.s   .check_pc
    sub.b   #'0',d2
    cmp.b   #7,d2
    bgt.s   .bad_reg

    ; A register - get address
    ext.w   d2
    lsl.w   #2,d2
    lea     saved_a0,a1
    add.w   d2,a1
    bra.w   .parse_value

.check_pc:
    ; Check for PC
    cmp.b   #'P',d1
    bne.s   .check_sr
    cmp.b   #'C',d2
    bne.s   .bad_reg
    lea     saved_pc,a1
    bra.w   .parse_value

.check_sr:
    ; Check for SR
    cmp.b   #'S',d1
    bne.s   .bad_reg
    cmp.b   #'R',d2
    bne.s   .bad_reg
    lea     saved_sr,a1
    bra.w   .parse_value

.parse_value:
    ; A1 now points to register storage
    bsr     SkipWhitespace
    bsr     ParseHex
    beq.s   .bad_value

    ; Store value (handle SR as word)
    cmp.l   #saved_sr,a1
    beq.s   .store_sr
    move.l  d0,(a1)
    bra.s   .ok

.store_sr:
    move.w  d0,(a1)

.ok:
    lea     .ok_msg(pc),a0
    bsr     SerialPutString
    bra.s   .done

.bad_reg:
    lea     .bad_reg_msg(pc),a0
    bsr     SerialPutString
    bra.s   .done

.bad_value:
    lea     .bad_val_msg(pc),a0
    bsr     SerialPutString
    bra.s   .done

.display_all:
    ; Use existing Panic serial output for register dump
    bsr     panic_serial_output

.done:
    movem.l (sp)+,d0-d3/a0-a2
    rts

.ok_msg:
    dc.b    "OK",0
.bad_reg_msg:
    dc.b    "Bad register name",0
.bad_val_msg:
    dc.b    "Bad hex value",0
    even

; ============================================================
; CmdMemory - Display or modify memory
; ============================================================
; Syntax: m[.b] <addr>   - Dump 16 bytes
;         m.w <addr>     - Dump 8 words
;         m.l <addr>     - Dump 4 longwords
;         m <addr> <val> - Write byte/word/long
;         m              - Dump next 16 bytes
; D0.w = mode (0=byte, 1=word, 2=long)
CmdMemory:
    movem.l d0-d5/a0-a1,-(sp)

    move.w  d0,d5                       ; Save mode (0=byte, 1=word, 2=long)

    lea     DBG_CMD_BUF,a0
    addq.l  #1,a0                       ; Skip 'm'

    ; Skip '.w' or '.l' suffix if present
    cmp.b   #'.',(a0)
    bne.s   .skip_done
    addq.l  #2,a0                       ; Skip '.w' or '.l'
.skip_done:
    bsr     SkipWhitespace

    ; Check if address follows
    move.b  (a0),d0
    bne     .parse_addr

    ; No address - use last address + 16
    move.l  DBG_LAST_ADDR,d0
    add.l   #16,d0
    bra     .have_addr

.parse_addr:
    bsr     ParseHex
    beq     .bad_addr

.have_addr:
    ; D0 = address
    move.l  d0,DBG_LAST_ADDR
    move.l  d0,a1                       ; Address to dump

    ; Check if value follows (write mode)
    bsr     SkipWhitespace
    move.b  (a0),d0
    beq     .dump_mode

    ; Parse value
    bsr     ParseHex
    beq     .bad_value

    ; Write based on digit count (D1 from ParseHex)
    cmp.w   #2,d1
    ble.s   .write_byte
    cmp.w   #4,d1
    ble.s   .write_word

    ; 5-8 digits: longword
    move.l  d0,(a1)
    lea     .ok_long_msg(pc),a0
    bra.s   .write_ok

.write_word:
    move.w  d0,(a1)
    lea     .ok_word_msg(pc),a0
    bra.s   .write_ok

.write_byte:
    move.b  d0,(a1)
    lea     .ok_byte_msg(pc),a0

.write_ok:
    bsr     SerialPutString
    bra     .done

.dump_mode:
    ; Print address
    move.l  a1,d0
    move.l  d0,-(sp)
    move.b  #'$',d0
    bsr     SerialPutChar
    move.l  (sp)+,d0
    bsr     SerialPutHex32

    lea     .colon(pc),a0
    bsr     SerialPutString

    ; Branch based on mode
    tst.w   d5
    beq.s   .dump_bytes
    cmp.w   #1,d5
    beq.s   .dump_words
    bra.s   .dump_longs

.dump_bytes:
    ; 16 bytes
    moveq   #15,d4
.dump_byte_loop:
    move.b  #' ',d0
    bsr     SerialPutChar
    move.b  (a1)+,d0
    bsr     SerialPutHex8
    dbf     d4,.dump_byte_loop
    bra     .done

.dump_words:
    ; 8 words (16 bytes)
    moveq   #7,d4
.dump_word_loop:
    move.b  #' ',d0
    bsr     SerialPutChar
    move.w  (a1)+,d0
    bsr     SerialPutHex16
    dbf     d4,.dump_word_loop
    bra     .done

.dump_longs:
    ; 4 longwords (16 bytes)
    moveq   #3,d4
.dump_long_loop:
    move.b  #' ',d0
    bsr     SerialPutChar
    move.l  (a1)+,d0
    bsr     SerialPutHex32
    dbf     d4,.dump_long_loop
    bra     .done

.bad_addr:
    lea     .bad_addr_msg(pc),a0
    bsr     SerialPutString
    bra     .done

.bad_value:
    lea     .bad_val_msg(pc),a0
    bsr     SerialPutString

.done:
    movem.l (sp)+,d0-d5/a0-a1
    rts

.ok_byte_msg:
    dc.b    "OK (byte)",13,10,0
.ok_word_msg:
    dc.b    "OK (word)",13,10,0
.ok_long_msg:
    dc.b    "OK (long)",13,10,0
.colon:
    dc.b    ": ",0
.bad_addr_msg:
    dc.b    "Bad address",0
.bad_val_msg:
    dc.b    "Bad value",0
    even

; ============================================================
; CmdGo - Continue execution
; ============================================================
; Syntax: g       - Continue from saved PC
;         g <addr> - Continue from specified address
CmdGo:
    movem.l d0/a0,-(sp)

    lea     DBG_CMD_BUF,a0
    addq.l  #1,a0                       ; Skip 'g'
    bsr     SkipWhitespace

    ; Check if address follows
    move.b  (a0),d0
    beq.s   .use_saved_pc

    ; Parse address
    bsr     ParseHex
    beq.s   .bad_addr

    ; Update saved PC
    move.l  d0,saved_pc

.use_saved_pc:
    lea     .msg(pc),a0
    bsr     SerialPutString

    ; Restore registers and continue
    ; Build RTE frame on stack
    move.l  saved_pc,-(sp)               ; PC
    move.w  saved_sr,-(sp)               ; SR

    ; Restore all registers
    movem.l saved_regs,d0-d7/a0-a6

    ; Return from exception (continues execution)
    rte

.bad_addr:
    lea     .bad_addr_msg(pc),a0
    bsr     SerialPutString
    movem.l (sp)+,d0/a0
    rts

.msg:
    dc.b    "Continuing...",10,13,0
.bad_addr_msg:
    dc.b    "Bad address",0
    even

; ============================================================
; CmdHelp - Display command help
; ============================================================
CmdHelp:
    movem.l a0,-(sp)

    lea     .help_text(pc),a0
    bsr     SerialPutString

    movem.l (sp)+,a0
    rts

.help_text:
    dc.b    10,13
    dc.b    "Commands:",10,13
    dc.b    "  r              Display all registers",10,13
    dc.b    "  r <reg> <hex>  Set register (D0-D7,A0-A7,PC,SR)",10,13
    dc.b    "  m[.b] <addr>   Memory dump as bytes",10,13
    dc.b    "  m.w <addr>     Memory dump as words",10,13
    dc.b    "  m.l <addr>     Memory dump as longs",10,13
    dc.b    "  m <addr> <hex> Write memory (1-2=byte,3-4=word,5-8=long)",10,13
    dc.b    "  g              Continue execution",10,13
    dc.b    "  g <addr>       Continue from address",10,13
    dc.b    "  ?              This help",10,13
    dc.b    0
    even
