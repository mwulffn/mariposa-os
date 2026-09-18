; ============================================================
; sprintf.s - Stack-based formatted output
; ============================================================
; Provides printf-style formatting for debug output
; ============================================================

; ============================================================
; Sprintf - Format string with arguments
; ============================================================
; Stack layout (caller pushes right-to-left):
;   SP+0:  Return address
;   SP+4:  Format string pointer
;   SP+8:  First argument
;   SP+12: Second argument, etc.
;
; Format specifiers:
;   %x   - Hex, long by default (8 digits)
;   %d   - Unsigned decimal, full 32-bit range
;   %b   - Binary, long by default (32 digits)
;   %s   - String
;   %%   - Literal '%'
;
; Size modifier (optional), accepted on either side of the specifier, so
; %x.b and %.bx mean the same thing:
;   .b - byte (2 hex digits / 8 binary digits)
;   .w - word (4 / 16)
;   .l - long (8 / 32)
; A '.' not followed by b, w or l is literal text, so "%d.%d" still works.
;
; Width specifier (optional), one or more digits before the specifier,
; clamped to 8. Applies to hex only:
;   %08x, %8x - 8 digits, zero filled
;
; Arguments are always longword stack slots regardless of size modifier.
;
; Returns:
;   A0 = SPRINTF_BUFFER pointer
;   D0.l = string length
; All other registers preserved
Sprintf:
    movem.l d1-d7/a1-a6,-(sp)

    ; A1 = destination buffer
    lea     SPRINTF_BUFFER,a1
    move.l  a1,a5               ; Save buffer start

    ; A2 = format string
    move.l  60(sp),a2           ; Get format string from stack

    ; A3 = argument pointer (points to first arg)
    lea     64(sp),a3

.loop:
    move.b  (a2)+,d0
    beq     .done

    cmp.b   #'%',d0
    bne     .literal

    ; Parse format specifier
    moveq   #0,d1               ; Width (0 = no padding)
    moveq   #'l',d2             ; Size (.b/.w/.l), long by default

    ; Optional width: one or more digits, so "%08x" works and not just "%8x".
    ; A leading zero is decoration - FormatHexToBuffer always emits exactly
    ; the digit count it is given, zero filled.
.width_loop:
    move.b  (a2),d0
    cmp.b   #'0',d0
    blt.s   .width_done
    cmp.b   #'9',d0
    bgt.s   .width_done
    mulu    #10,d1
    and.w   #$0F,d0
    add.w   d0,d1
    addq.l  #1,a2
    bra.s   .width_loop

.width_done:
    ; A 32-bit value is 8 hex digits at most, and a wider count would
    ; overflow the byte-sized shift in FormatHexToBuffer.
    cmp.w   #8,d1
    bls.s   .size_before
    moveq   #8,d1

.size_before:
    ; Size modifier ahead of the specifier: "%.lx". Only consume the '.'
    ; when a size letter really follows it.
    cmp.b   #'.',(a2)
    bne.s   .get_spec
    move.b  1(a2),d3
    cmp.b   #'b',d3
    beq.s   .take_before
    cmp.b   #'w',d3
    beq.s   .take_before
    cmp.b   #'l',d3
    bne.s   .get_spec
.take_before:
    move.b  d3,d2
    addq.l  #2,a2

.get_spec:
    ; Get specifier
    move.b  (a2)+,d0

    ; Check for literal '%'
    cmp.b   #'%',d0
    beq     .literal

    ; Size modifier after the specifier: "%x.l". This is the form almost
    ; every caller in this ROM writes, so it has to be accepted too.
    ;
    ; Only for specifiers where a size means something. Without that gate
    ; "%s.bin" would swallow the ".b" and print "in", and "%d.log" would
    ; lose its ".l" - a trap for whoever writes the next format string.
    cmp.b   #'x',d0
    beq.s   .want_suffix
    cmp.b   #'b',d0
    bne.s   .check_spec
.want_suffix:
    ; And only when a size letter really follows, so "%x.txt" keeps its dot.
    cmp.b   #'.',(a2)
    bne.s   .check_spec
    move.b  1(a2),d3
    cmp.b   #'b',d3
    beq.s   .take_after
    cmp.b   #'w',d3
    beq.s   .take_after
    cmp.b   #'l',d3
    bne.s   .check_spec
.take_after:
    move.b  d3,d2
    addq.l  #2,a2

.check_spec:
    cmp.b   #'x',d0
    beq.s   .hex
    cmp.b   #'d',d0
    beq.s   .dec
    cmp.b   #'b',d0
    beq.s   .bin
    cmp.b   #'s',d0
    beq.s   .str
    bra     .loop               ; Unknown specifier, skip

.hex:
    ; Get argument based on size. Callers always push longword slots,
    ; whatever width they asked to display, so every path reads a long and
    ; then narrows - reading a word here took the high half of the slot.
    cmp.b   #'b',d2
    beq.s   .hex_byte
    cmp.b   #'w',d2
    beq.s   .hex_word

.hex_long:
    move.l  (a3)+,d3            ; Get long argument
    moveq   #8,d4               ; 8 digits
    bra.s   .do_hex

.hex_word:
    move.l  (a3)+,d3
    and.l   #$FFFF,d3           ; Narrow to a word
    moveq   #4,d4               ; 4 digits
    bra.s   .do_hex

.hex_byte:
    move.l  (a3)+,d3
    and.l   #$FF,d3             ; Narrow to a byte
    moveq   #2,d4               ; 2 digits

.do_hex:
    ; Apply width padding if specified
    tst.b   d1
    beq.s   .hex_no_pad
    move.b  d1,d4               ; Use width instead

.hex_no_pad:
    bsr     FormatHexToBuffer
    bra     .loop

.dec:
    move.l  (a3)+,d3            ; Get argument
    bsr     FormatDecToBuffer
    bra     .loop

.bin:
    ; Get argument based on size - longword slots again, as above.
    cmp.b   #'b',d2
    beq.s   .bin_byte
    cmp.b   #'w',d2
    beq.s   .bin_word

.bin_long:
    move.l  (a3)+,d3
    moveq   #32,d4              ; 32 bits
    bra.s   .do_bin

.bin_word:
    move.l  (a3)+,d3
    and.l   #$FFFF,d3
    moveq   #16,d4              ; 16 bits
    bra.s   .do_bin

.bin_byte:
    move.l  (a3)+,d3
    and.l   #$FF,d3
    moveq   #8,d4               ; 8 bits

.do_bin:
    bsr     FormatBinToBuffer
    bra     .loop

.str:
    move.l  (a3)+,a4            ; Get string pointer
.str_loop:
    move.b  (a4)+,d0
    beq     .loop
    move.b  d0,(a1)+
    bra.s   .str_loop

.literal:
    move.b  d0,(a1)+
    bra     .loop

.done:
    clr.b   (a1)                ; Null terminate

    ; Calculate length
    move.l  a1,d0
    sub.l   a5,d0               ; Length = end - start

    move.l  a5,a0               ; Return buffer pointer
    movem.l (sp)+,d1-d7/a1-a6
    rts

; ============================================================
; FormatHexToBuffer - Convert value to hex and append to buffer
; ============================================================
; D3.l = value
; D4.b = number of digits
; A1 = buffer pointer (updated)
; Modifies: D3-D6
FormatHexToBuffer:
    movem.l d3-d6,-(sp)

    ; Calculate shift amount
    move.b  d4,d5
    subq.b  #1,d5
    lsl.b   #2,d5               ; Shift = (digits-1) * 4

.loop:
    move.l  d3,d6
    lsr.l   d5,d6               ; Shift right to get nibble
    and.w   #$0F,d6

    cmp.b   #10,d6
    blt.s   .digit
    add.b   #'A'-10,d6
    bra.s   .store
.digit:
    add.b   #'0',d6
.store:
    move.b  d6,(a1)+

    subq.b  #4,d5               ; Next nibble
    subq.b  #1,d4
    bne.s   .loop

    movem.l (sp)+,d3-d6
    rts

; ============================================================
; FormatDecToBuffer - Convert value to decimal and append to buffer
; ============================================================
; D3.l = value (unsigned, full 32-bit range)
; A1 = buffer pointer (updated)
; Modifies: A1 only. A6 is used as scratch by the reversal below.
FormatDecToBuffer:
    movem.l d0-d6/a4,-(sp)

    ; Handle zero
    tst.l   d3
    bne.s   .convert
    move.b  #'0',(a1)+
    bra.s   .done

.convert:
    ; Store digits in reverse
    move.l  a1,a4               ; Save start

.digit_loop:
    ; divu32_10 rather than divu.w #10: divu.w is 32/16 -> 16, so it
    ; overflows as soon as the quotient passes 65535 - that is, for any
    ; value from 655360 up, which partition LBAs and block counts reach
    ; easily. On overflow the 68000 leaves the destination untouched.
    move.l  d3,d0
    bsr     divu32_10           ; D0 = quotient, D1 = remainder
    add.b   #'0',d1
    move.b  d1,(a1)+
    move.l  d0,d3
    tst.l   d3
    bne.s   .digit_loop

    ; Reverse string
    move.l  a1,d6               ; End position
    move.l  a4,d5               ; Start position
    subq.l  #1,d6

.reverse:
    cmp.l   d5,d6
    ble.s   .done

    move.l  d5,a6
    move.b  (a6),d3             ; Swap bytes
    move.l  d6,a6
    move.b  (a6),d4
    move.l  d5,a6
    move.b  d4,(a6)
    move.l  d6,a6
    move.b  d3,(a6)

    addq.l  #1,d5
    subq.l  #1,d6
    bra.s   .reverse

.done:
    movem.l (sp)+,d0-d6/a4
    rts

; ============================================================
; FormatBinToBuffer - Convert value to binary and append to buffer
; ============================================================
; D3.l = value
; D4.b = number of bits
; A1 = buffer pointer (updated)
; Modifies: D3-D6
FormatBinToBuffer:
    movem.l d3-d6,-(sp)

    ; Calculate shift amount
    move.b  d4,d5
    subq.b  #1,d5               ; Shift = bits-1

.loop:
    move.l  d3,d6
    lsr.l   d5,d6
    and.w   #1,d6
    add.b   #'0',d6
    move.b  d6,(a1)+

    subq.b  #1,d5
    subq.b  #1,d4
    bne.s   .loop

    movem.l (sp)+,d3-d6
    rts

; ============================================================
; SerialPrintf - Format and print to serial port
; ============================================================
; Stack layout same as Sprintf
; Convenience wrapper: calls Sprintf then serial_put_string
SerialPrintf:
    bsr     Sprintf
    bsr     serial_put_string
    rts
