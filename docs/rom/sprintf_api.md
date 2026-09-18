# Sprintf API

Stack-based formatted output for ROM debug messages. Implemented in
`src/rom/sprintf.s`, covered by the `format.*` tests in `tests/test_format.c`.

## Functions

### SerialPrintf

Format and send to the serial port. **This is the entry point to use.**

```
SP+0:  Return address
SP+4:  Format string pointer
SP+8:  First argument
SP+12: Second argument, etc.
```

Caller pushes right to left and cleans up. Returns nothing. All registers
preserved.

### Sprintf

Formats into `SPRINTF_BUFFER` ($3400, 256 bytes) and returns `A0` = buffer,
`D0.l` = length.

> **Not callable directly.** `Sprintf` reads its format pointer at a fixed
> stack offset that assumes it was reached by `bsr` from `SerialPrintf`. A
> direct `bsr Sprintf` picks up the first argument as the format string.
> Use `SerialPrintf`, or fix the offset first if you need a string back.

## Format syntax

```
%[width][.size]specifier[.size]
```

| Specifier | Description | Default |
|-----------|-------------|---------|
| `%x` | Hexadecimal, uppercase | long, 8 digits |
| `%d` | Unsigned decimal, full 32-bit range | — |
| `%b` | Binary | long, 32 digits |
| `%s` | Null-terminated string | — |
| `%%` | Literal `%` | — |

### Size modifier

Accepted on **either side** of the specifier — `%x.b` and `%.bx` are the same
thing. Most callers in this ROM write the suffix form; `memory.s` writes the
prefix form.

| Size | Hex digits | Binary digits |
|------|-----------|---------------|
| `.b` | 2 | 8 |
| `.w` | 4 | 16 |
| `.l` | 8 (default) | 32 (default) |

A suffix is only consumed after `%x` and `%b`, where a size means something,
and only when a size letter actually follows the dot. So these keep their
dots as literal text:

```
"%s.bin"   ->  SYSTEM.bin
"%d.log"   ->  3.log
"v%d.%d"   ->  v1.5
"%x.txt"   ->  0000002A.txt
```

### Width modifier

One or more digits before the specifier, clamped to 8. Output is always zero
filled to the digit count, so `%08x` and `%8x` are equivalent — the leading
zero is decoration, not a flag. Applies to hex only.

| Format | Value | Output |
|--------|-------|--------|
| `%x` | `$1234` | `00001234` |
| `%08x` | `$1234` | `00001234` |
| `%4x` | `$DEADBEEF` | `BEEF` |
| `%08x.b` | `$DEADBEAB` | `000000AB` |

## Arguments are always longwords

**Every argument occupies one 32-bit stack slot, whatever size you ask to
display.** The size modifier narrows the value for printing; it does not
change how much stack the argument takes. Push bytes and words as longs.

```asm
    move.l  d1,-(sp)            ; second argument, a full long
    move.l  d0,-(sp)            ; first argument, a full long
    pea     .fmt(pc)
    bsr     SerialPrintf
    lea     12(sp),sp           ; 4 + 4 + 4
```

## Examples

### Hex and decimal

```asm
    move.l  d0,-(sp)
    pea     .fmt(pc)
    bsr     SerialPrintf
    addq.l  #8,sp

.fmt:
    dc.b    "D0: $%x.l",10,13,0
    even
```

Output: `D0: $12345678`

### Multiple arguments

```asm
    move.l  d1,-(sp)
    move.l  d0,-(sp)
    pea     .fmt(pc)
    bsr     SerialPrintf
    lea     12(sp),sp

.fmt:
    dc.b    "D0=$%x.l D1=$%x.l",10,13,0
    even
```

Output: `D0=$12345678 D1=$ABCDEF00`

### Mixed sizes

```asm
    moveq   #0,d0
    move.b  status,d0
    move.l  d0,-(sp)            ; byte value, still a long on the stack
    move.l  #42,-(sp)
    pea     .fmt(pc)
    bsr     SerialPrintf
    lea     12(sp),sp           ; 4 + 4 + 4, not 4 + 4 + 2

.fmt:
    dc.b    "Count: %d, Status: $%x.b",10,13,0
    even
```

Output: `Count: 42, Status: $AB`

### Binary and string

```asm
    move.l  #%10101010,-(sp)
    pea     .str(pc)
    pea     .fmt(pc)
    bsr     SerialPrintf
    lea     12(sp),sp

.fmt:
    dc.b    "%s: %b.b",10,13,0
.str:
    dc.b    "Flags",0
    even
```

Output: `Flags: 10101010`

## Implementation notes

**Buffer:** `SPRINTF_BUFFER` at $3400, 256 bytes, shared with
`serial_put_hex32` and `serial_put_decimal`.

**Decimal:** `%d` handles the full unsigned 32-bit range. It divides through
`divu32_10` in `src/rom/math.s`, because the 68000's `divu.w` is 32/16 → 16
and overflows for any value from 655360 up.

**Unknown specifiers** are dropped and their argument is *not* consumed, so
everything after them is misaligned. There is no diagnostic for this.

**No bounds checking.** Output beyond 256 bytes runs past the end of the
buffer. Nothing enforces the limit.

## Limitations

- No signed decimal — `%d` is unsigned only
- No lowercase hex
- No floating point
- Width applies to hex only, not binary or decimal
- `Sprintf` is not directly callable (see above)
- No buffer overflow protection
