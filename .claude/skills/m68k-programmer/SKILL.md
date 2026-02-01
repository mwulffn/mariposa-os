---
name: m68k-programmer
description: Use this skill when implementing assembly code on the motorola 68000 cpu with vasm
---
# M68K Amiga Development

Expert knowledge for Motorola 68000 assembly programming on Amiga hardware.

## When to Use This Skill

Invoke this skill when:
- Writing or debugging 68000 assembly code
- Working with Amiga custom chip registers
- Troubleshooting hardware access issues
- Setting up serial debugging
- Dealing with assembly syntax errors

## Assembler: VASM (Motorola syntax)

```bash
vasmm68k_mot -Fbin -m68000 -no-opt -I. -o build/kick.rom bootstrap.s
```

### Reserved words - DO NOT use as labels:
- DEBUG, RESET, AND, OR, NOT, EOR
- Use alternatives: Panic, Start, AndMask, etc.

### Syntax notes:
- Local labels: `.loop`, `.done` (dot prefix)
- Hex: `$DFF000` or `0xDFF000`
- Binary: `%00110011`
- Include: `include "file.i"`
- Equates: `LABEL equ $1234`

## 68000 Gotchas

### Word alignment
- Word/longword access must be even addresses
- `dc.b` followed by `dc.w` needs `even` directive

```asm
MyString:   dc.b    "Hello",0
            even                ; REQUIRED before word data
MyWord:     dc.w    $1234
```

### Address registers
- Can't do byte operations: `move.b d0,a0` is illegal
- LEA for address loading, MOVE.L for data registers

### MOVEM order
- Register list is always D0-D7/A0-A7 in encoding
- `movem.l d0-d7/a0-a6,-(sp)` - saves in reverse order
- `movem.l (sp)+,d0-d7/a0-a6` - restores correctly

### Stack
- Pre-decrement push: `move.l d0,-(sp)`
- Post-increment pop: `move.l (sp)+,d0`
- SSP must be even

## Amiga Hardware Patterns

### Chip register access convention
```asm
    lea     CUSTOM,a6           ; $DFF000
    move.w  #$7FFF,DMACON(a6)   ; Access via offset
```

### ROM overlay - CRITICAL
Before accessing chip RAM at $000000-$07FFFF:
```asm
    move.b  #$03,CIAA_DDRA      ; Bits 0,1 as outputs
    move.b  #$00,CIAA_PRA       ; Clear OVL bit
```
Failure to do this = writes to chip RAM fail silently.

### CIA access
CIA registers are at ODD addresses only:
```asm
CIAA_PRA    equ $BFE001         ; Note: odd address
CIAA_DDRA   equ $BFE201
```

### Wait for blitter
```asm
WaitBlit:
    btst    #6,DMACONR(a6)      ; BBUSY flag
    bne.s   WaitBlit
    rts
```

### Wait for vertical blank
```asm
WaitVBL:
    move.l  VPOSR(a6),d0
    and.l   #$1FF00,d0
    cmp.l   #$00000,d0          ; Line 0
    bne.s   WaitVBL
    rts
```

### Color for debug
Quick visual feedback:
```asm
    move.w  #$F00,COLOR00(a6)   ; Red = reached this point
```

### Serial output (polling)
```asm
SerPutc:                        ; D0.b = character
    btst    #13,SERDATR(a6)     ; TBE?
    beq.s   SerPutc
    and.w   #$00FF,d0
    or.w    #$0100,d0           ; Stop bit
    move.w  d0,SERDAT(a6)
    rts
```

## Project Conventions

See `docs/ROM_DESIGN.md` for:
- Memory map
- Kernel entry conditions (A0, A1, A7)
- Exception handling design
- Boot sequence

### Error handling
- Never hang silently
- All errors → debugger with message
- Exception: chip RAM failure → yellow screen ($FF0) + halt

## Testing with FS-UAE

### Serial socket for automated testing
```ini
# a500.fs-uae
serial_port = tcp://127.0.0.1:5555
```

Workflow:
1. Start listener: `nc -l 5555 > serial.log &`
2. Run emulator: `make run`
3. Wait for output / timeout
4. Kill emulator
5. Check serial.log for expected output

Use ROM serial output to verify code execution:
```asm
    lea     .msg(pc),a0
    bsr     SerPuts
.msg:
    dc.b    "TEST OK",13,10,0
    even
```

### ROM debugger
Once the ROM debugger is complete, Claude Code can interact with it via serial socket - examine memory, registers, step through code. This is the primary debugging tool.

### FS-UAE debugger (human only)
F12+D enters FS-UAE's built-in debugger - not accessible to Claude Code.

## Common Issues and Solutions

### Assembly fails with "illegal use of reserved word"
- Check if using DEBUG, RESET, AND, OR, NOT, EOR as labels
- Rename to Panic, Start, AndMask, etc.

### "Address error" exception
- Check word/longword alignment (must be even addresses)
- Add `even` directive after byte data

### Chip RAM writes don't work
- ROM overlay not disabled - clear CIAA_PRA bit 0 first
- Check that writes are to $000000-$07FFFF range

### Serial output not appearing
- Verify SERPER is set (baud rate)
- Check TBE (transmit buffer empty) flag before writing
- Ensure FS-UAE serial_port config is correct

### Code hangs without visible error
- Use color debugging: `move.w #$F00,COLOR00(a6)`
- Add serial debug output at key points
- Check if stuck in wait loop (blitter, vblank)

## Quick Reference Card

| Task | Code Pattern |
|------|--------------|
| Disable ROM overlay | `move.b #$00,CIAA_PRA` |
| Set custom chip base | `lea CUSTOM,a6` |
| Write register | `move.w value,REGISTER(a6)` |
| Save all registers | `movem.l d0-d7/a0-a6,-(sp)` |
| Restore registers | `movem.l (sp)+,d0-d7/a0-a6` |
| Debug color | `move.w #$XXX,COLOR00(a6)` |
| Serial char out | See SerPutc pattern above |
| Wait blitter | See WaitBlit pattern above |
| Align data | `even` directive |
| Local label | `.label_name` |
