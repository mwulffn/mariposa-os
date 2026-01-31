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
;   %x.b - Hex byte (2 digits)
;   %x.w - Hex word (4 digits)
;   %x.l - Hex long (8 digits, default)
;   %d   - Unsigned decimal
;   %b.b - Binary byte
;   %b.w - Binary word
;   %s   - String
;   %%   - Literal '%'
;
; Width specifier (optional):
;   %08x - Pad with zeros to 8 digits
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
    moveq   #'l',d2             ; Size (.b/.w/.l)

    ; Check for width specifier
    move.b  (a2),d0
    cmp.b   #'0',d0
    blt.s   .no_width
    cmp.b   #'9',d0
    bgt.s   .no_width

    ; Parse width
    sub.b   #'0',d0
    move.b  d0,d1
    addq.l  #1,a2

.no_width:
    ; Get specifier
    move.b  (a2)+,d0

    ; Check for literal '%'
    cmp.b   #'%',d0
    beq     .literal

    ; Check for size modifier
    cmp.b   #'.',d0
    bne.s   .check_spec
    move.b  (a2)+,d2            ; Get size (.b/.w/.l)
    move.b  (a2)+,d0            ; Get actual specifier

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
    ; Get argument based on size
    cmp.b   #'b',d2
    beq.s   .hex_byte
    cmp.b   #'w',d2
    beq.s   .hex_word

.hex_long:
    move.l  (a3)+,d3            ; Get long argument
    moveq   #8,d4               ; 8 digits
    bra.s   .do_hex

.hex_word:
    moveq   #0,d3
    move.w  (a3)+,d3            ; Get word argument
    moveq   #4,d4               ; 4 digits
    bra.s   .do_hex

.hex_byte:
    moveq   #0,d3
    move.w  (a3)+,d3            ; Get byte as word
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
    ; Get argument based on size
    cmp.b   #'w',d2
    beq.s   .bin_word

.bin_byte:
    moveq   #0,d3
    move.w  (a3)+,d3
    moveq   #8,d4               ; 8 bits
    bra.s   .do_bin

.bin_word:
    moveq   #0,d3
    move.w  (a3)+,d3
    moveq   #16,d4              ; 16 bits

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
; D3.l = value
; A1 = buffer pointer (updated)
; Modifies: D3-D6, A4
FormatDecToBuffer:
    movem.l d3-d6/a4,-(sp)

    ; Handle zero
    tst.l   d3
    bne.s   .convert
    move.b  #'0',(a1)+
    bra.s   .done

.convert:
    ; Store digits in reverse
    move.l  a1,a4               ; Save start

.digit_loop:
    move.l  d3,d4
    divu    #10,d4
    swap    d4                  ; Remainder in low word
    add.b   #'0',d4
    move.b  d4,(a1)+
    clr.w   d4
    swap    d4
    move.l  d4,d3
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
    movem.l (sp)+,d3-d6/a4
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
; Convenience wrapper: calls Sprintf then SerialPutString
SerialPrintf:
    bsr     Sprintf
    bsr     SerialPutString
    rts
