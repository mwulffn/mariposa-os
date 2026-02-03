# Sprintf API Documentation

## Overview

The ROM provides stack-based `Sprintf` and `SerialPrintf` functions for formatted debug output. These functions simplify debug printing by eliminating the need for multiple function calls and manual buffer management.

## Functions

### Sprintf

Formats a string with arguments into a 256-byte buffer at `$3400`.

**Stack Layout (caller pushes right-to-left):**
```
SP+0:  Return address
SP+4:  Format string pointer
SP+8:  First argument
SP+12: Second argument, etc.
```

**Returns:**
- `A0` = pointer to `SPRINTF_BUFFER` ($3400)
- `D0.l` = string length
- All other registers preserved

### SerialPrintf

Convenience wrapper that calls `Sprintf` followed by `SerialPutString`.

**Stack Layout:** Same as `Sprintf`

**Returns:** Nothing (output sent to serial port)

## Format Specifiers

### Basic Syntax

```
%[width][.size]specifier
```

### Specifiers

| Specifier | Description | Example Input | Example Output |
|-----------|-------------|---------------|----------------|
| `%x` | Hexadecimal (uppercase A-F) | `$12AB` | `12AB` |
| `%d` | Unsigned decimal | `42` | `42` |
| `%b` | Binary | `$5` | `0101` |
| `%s` | Null-terminated string | `"Hello"` | `Hello` |
| `%%` | Literal '%' character | N/A | `%` |

### Size Modifiers

| Size | Description | Default Width |
|------|-------------|---------------|
| `.b` | Byte (8-bit) | 2 hex, 8 binary |
| `.w` | Word (16-bit) | 4 hex, 16 binary |
| `.l` | Long (32-bit) | 8 hex (default) |

### Width Modifier

Pads output with leading zeros to specified width (0-9 digits).

| Format | Value | Output |
|--------|-------|--------|
| `%x.l` | `$1234` | `00001234` |
| `%8x` | `$1234` | `00001234` |
| `%4x.w` | `$AB` | `00AB` |
| `%2x.b` | `$F` | `0F` |

**Note:** Width overrides default size width.

## Examples

### Example 1: Basic Hex Output

```asm
    move.l  d0,-(sp)            ; Push value
    pea     .fmt(pc)            ; Push format string
    bsr     Sprintf
    addq.l  #8,sp               ; Clean stack
    bsr     SerialPutString     ; A0 already points to buffer

.fmt:
    dc.b    "D0: $%x.l",10,13,0
    even
```

**Output:** `D0: $12345678`

### Example 2: Using SerialPrintf (Simpler)

```asm
    move.l  d0,-(sp)
    pea     .fmt(pc)
    bsr     SerialPrintf
    addq.l  #8,sp

.fmt:
    dc.b    "D0: $%x.l",10,13,0
    even
```

**Output:** `D0: $12345678`

### Example 3: Multiple Arguments

```asm
    move.l  d1,-(sp)            ; Second value
    move.l  d0,-(sp)            ; First value
    pea     .fmt(pc)
    bsr     SerialPrintf
    lea     12(sp),sp           ; Clean 3 items (4+4+4 bytes)

.fmt:
    dc.b    "D0=$%x.l D1=$%x.l",10,13,0
    even
```

**Output:** `D0=$12345678 D1=$ABCDEF00`

### Example 4: Mixed Formats

```asm
    move.w  #$AB,-(sp)          ; Byte value (pushed as word)
    move.l  #42,-(sp)           ; Decimal value
    pea     .fmt(pc)
    bsr     SerialPrintf
    lea     10(sp),sp           ; Clean up (4+4+2 = 10 bytes)

.fmt:
    dc.b    "Count: %d, Status: $%x.b",10,13,0
    even
```

**Output:** `Count: 42, Status: $AB`

### Example 5: Binary and String

```asm
    pea     .str(pc)            ; String pointer
    move.w  #%10101010,-(sp)    ; Binary value
    pea     .fmt(pc)
    bsr     SerialPrintf
    lea     10(sp),sp

.fmt:
    dc.b    "%s: %b.b",10,13,0
.str:
    dc.b    "Flags",0
    even
```

**Output:** `Flags: 10101010`

### Example 6: Width Padding

```asm
    move.l  #$1234,-(sp)
    pea     .fmt(pc)
    bsr     SerialPrintf
    addq.l  #8,sp

.fmt:
    dc.b    "Address: $%08x",10,13,0
    even
```

**Output:** `Address: $00001234`

## Common Patterns

### Debug Register Dump

```asm
DebugDumpRegs:
    movem.l d0-d1/a0,-(sp)

    move.l  d0,-(sp)
    pea     .d0_fmt(pc)
    bsr     SerialPrintf
    addq.l  #8,sp

    move.l  d1,-(sp)
    pea     .d1_fmt(pc)
    bsr     SerialPrintf
    addq.l  #8,sp

    movem.l (sp)+,d0-d1/a0
    rts

.d0_fmt:
    dc.b    "D0: $%x.l",10,13,0
.d1_fmt:
    dc.b    "D1: $%x.l",10,13,0
    even
```

### Memory Dump Line

```asm
    move.l  (a0),-(sp)          ; Memory value
    move.l  a0,-(sp)            ; Address
    pea     .fmt(pc)
    bsr     SerialPrintf
    lea     12(sp),sp

.fmt:
    dc.b    "$%x.l: $%x.l",10,13,0
    even
```

**Output:** `$00080000: $12345678`

### Status Message with Hex Value

```asm
    move.l  d0,-(sp)
    pea     .msg(pc)
    bsr     SerialPrintf
    addq.l  #8,sp

.msg:
    dc.b    "Chip RAM detected: $%x.l bytes",10,13,0
    even
```

## Implementation Notes

### Buffer Location

- Output buffer: `SPRINTF_BUFFER` at `$3400`
- Size: 256 bytes
- Shared by `Sprintf`, `SerialPutHex32`, and `SerialPutDecimal`

### Register Usage

`Sprintf` preserves all registers except:
- `A0` = buffer pointer (return value)
- `D0.l` = string length (return value)

### Stack Alignment

Arguments should be aligned:
- Byte values: push as words (2 bytes minimum)
- Word values: push as words (2 bytes)
- Long values: push as longs (4 bytes)
- Pointers: push as longs (4 bytes)

### Cleanup

Caller must clean the stack after the call:
```asm
    ; Example: 2 longs + format pointer
    move.l  arg2,-(sp)      ; 4 bytes
    move.l  arg1,-(sp)      ; 4 bytes
    pea     format          ; 4 bytes
    bsr     SerialPrintf
    lea     12(sp),sp       ; Clean 12 bytes total
```

### Null Termination

`Sprintf` always null-terminates the output string.

## Migration from Old API

### Before (Multiple Calls)

```asm
    lea     .msg(pc),a0
    bsr     SerialPutString
    move.b  #'$',d0
    bsr     SerialPutChar
    move.l  d0,d0
    bsr     SerialPutHex32
    lea     .nl(pc),a0
    bsr     SerialPutString

.msg:  dc.b "Value: ",0
.nl:   dc.b 10,13,0
```

### After (Single Call)

```asm
    move.l  d0,-(sp)
    pea     .fmt(pc)
    bsr     SerialPrintf
    addq.l  #8,sp

.fmt:
    dc.b    "Value: $%x.l",10,13,0
    even
```

## Limitations

- Maximum output length: 255 bytes (256 - null terminator)
- Width specifier: single digit only (0-9)
- No floating point support
- No signed decimal (only unsigned)
- No lowercase hex
- Format string must be in accessible memory (ROM or RAM)

## Error Handling

`Sprintf` does not validate:
- Format string correctness
- Argument count matching format specifiers
- Buffer overflow (output silently truncated at 255 bytes)

Caller must ensure:
- Format string is null-terminated
- Correct number and type of arguments
- Output fits in 256 bytes
