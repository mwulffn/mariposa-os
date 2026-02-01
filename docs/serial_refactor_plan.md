# Serial.s Refactor Plan

## Objective
Refactor `src/rom/serial.s` to follow C-style (snake_case) label conventions and VBCC fastcall register conventions, consistent with the recently completed debugger.s refactor.

## Current State Analysis

### Current Label Conventions
- **Global labels**: PascalCase (e.g., `SerialInit`, `SerialPutChar`, `SerialPutString`)
- **Local labels**: Dot-prefix with lowercase (e.g., `.wait`, `.loop`, `.done`)

### Current Register Usage Analysis

All functions analyzed against VBCC fastcall convention:
- **Scratch (caller-save)**: D0, D1, A0, A1, CCR - can be trashed by callee
- **Preserved (callee-save)**: D2-D7, A2-A6 - must be saved if used

#### 1. `SerialInit` (line 13-16)
```asm
SerialInit:
    lea     CUSTOM,a6
    move.w  #$0170,SERPER(a6)
    rts
```
- **Uses**: A6 (preserved register)
- **Issue**: A6 is preserved but not saved - INCORRECT for a public API
- **Comment says**: "No parameters, modifies A6 only"
- **Fix**: Add `movem.l a6,-(sp)` / `movem.l (sp)+,a6`

#### 2. `SerialPutChar` (line 23-31)
```asm
SerialPutChar:
    movem.l d0/a6,-(sp)
    lea     CUSTOM,a6
.wait:
    btst    #SERDATR_TBE,SERDATR(a6)
    beq.s   .wait
    move.w  d0,SERDAT(a6)
    movem.l (sp)+,d0/a6
    rts
```
- **Parameter**: D0.b (scratch register - argument)
- **Uses**: D0, A6
- **Current save**: D0, A6
- **Issue**: D0 is scratch (argument register), doesn't need saving. A6 is preserved, needs saving.
- **Fix**: Change to `movem.l a6,-(sp)` / `movem.l (sp)+,a6`

#### 3. `SerialPutString` (line 38-47)
```asm
SerialPutString:
    movem.l d0/a0/a6,-(sp)
.loop:
    move.b  (a0)+,d0
    beq.s   .done
    bsr     SerialPutChar
    bra.s   .loop
.done:
    movem.l (sp)+,d0/a0/a6
    rts
```
- **Parameter**: A0 (scratch register - argument)
- **Uses**: D0, A0, A6
- **Current save**: D0, A0, A6
- **Issue**: D0 and A0 are scratch, don't need saving. A6 is preserved, needs saving.
- **Analysis**: After the fix to SerialPutChar, it won't trash A6 anymore, but we still use A6 locally
- **Fix**: Change to `movem.l a6,-(sp)` / `movem.l (sp)+,a6`

#### 4. `SerialGetChar` (line 54-66)
```asm
SerialGetChar:
    movem.l a6,-(sp)
    lea     CUSTOM,a6
    btst    #SERDATR_RBF,SERDATR(a6)
    beq.s   .no_data
    move.w  SERDATR(a6),d0
    and.w   #$00FF,d0
    movem.l (sp)+,a6
    rts
.no_data:
    moveq   #0,d0
    movem.l (sp)+,a6
    rts
```
- **Returns**: D0 (scratch register - return value)
- **Uses**: D0, A6
- **Current save**: A6 only
- **Analysis**: ✅ CORRECT - only saves preserved register A6

#### 5. `SerialWaitChar` (line 73-82)
```asm
SerialWaitChar:
    movem.l a6,-(sp)
    lea     CUSTOM,a6
.wait:
    btst    #SERDATR_RBF,SERDATR(a6)
    beq.s   .wait
    move.w  SERDATR(a6),d0
    and.w   #$00FF,d0
    movem.l (sp)+,a6
    rts
```
- **Returns**: D0 (scratch register - return value)
- **Uses**: D0, A6
- **Current save**: A6 only
- **Analysis**: ✅ CORRECT - only saves preserved register A6

#### 6. `SerialPutHex8` (line 89-119)
```asm
SerialPutHex8:
    movem.l d0-d2,-(sp)
    move.b  d0,d2                       ; Save value
    ; High nibble
    lsr.b   #4,d0
    ...
    bsr     SerialPutChar
    ; Low nibble
    move.b  d2,d0
    ...
    bsr     SerialPutChar
    movem.l (sp)+,d0-d2
    rts
```
- **Parameter**: D0.b (scratch register - argument)
- **Uses**: D0, D1, D2
- **Current save**: D0, D1, D2
- **Issue**: D0 and D1 are scratch. D2 is preserved and IS used, so it needs saving.
- **Analysis**: Comment says "Modifies: D0-D2, A6" but A6 is not saved here (relies on SerialPutChar)
- **Fix**: Change to `movem.l d2,-(sp)` / `movem.l (sp)+,d2`

#### 7. `SerialPutHex16` (line 126-136)
```asm
SerialPutHex16:
    movem.l d0-d1,-(sp)
    move.w  d0,d1                   ; Save value
    lsr.w   #8,d0                   ; Get high byte
    bsr     SerialPutHex8
    move.b  d1,d0                   ; Get low byte
    bsr     SerialPutHex8
    movem.l (sp)+,d0-d1
    rts
```
- **Parameter**: D0.w (scratch register - argument)
- **Uses**: D0, D1
- **Current save**: D0, D1
- **Issue**: Both D0 and D1 are scratch, don't need saving
- **Fix**: Remove movem entirely

#### 8. `SerialPutHex32` (line 143-167)
```asm
SerialPutHex32:
    movem.l d0-d2/a0,-(sp)
    lea     SPRINTF_BUFFER,a0
    moveq   #7,d2               ; 8 hex digits
.loop:
    rol.l   #4,d0
    move.l  d0,d1
    and.w   #$0F,d1
    cmp.b   #10,d1
    blt.s   .digit
    add.b   #'A'-10,d1
    bra.s   .store
.digit:
    add.b   #'0',d1
.store:
    move.b  d1,(a0)+
    dbf     d2,.loop
    clr.b   (a0)
    lea     SPRINTF_BUFFER,a0
    bsr     SerialPutString
    movem.l (sp)+,d0-d2/a0
    rts
```
- **Parameter**: D0.l (scratch register - argument)
- **Uses**: D0, D1, D2, A0
- **Current save**: D0, D1, D2, A0
- **Issue**: D0, D1, A0 are scratch. D2 is preserved and IS used (loop counter).
- **Fix**: Change to `movem.l d2,-(sp)` / `movem.l (sp)+,d2`

#### 9. `SerialPutDecimal` (line 174-219)
```asm
SerialPutDecimal:
    movem.l d0-d2/a0-a2,-(sp)
    lea     SPRINTF_BUFFER,a0
    move.l  a0,a1           ; Save start position
    ...
    [uses D0, D1, D2, A0, A1, A2]
    ...
    movem.l (sp)+,d0-d2/a0-a2
    rts
```
- **Parameter**: D0.l (scratch register - argument)
- **Uses**: D0, D1, D2, A0, A1, A2
- **Current save**: D0, D1, D2, A0, A1, A2
- **Issue**: D0, D1, A0, A1 are scratch. D2, A2 are preserved and ARE used.
- **Fix**: Change to `movem.l d2/a2,-(sp)` / `movem.l (sp)+,d2/a2`

### Register Convention Summary

| Function | Current Save | Should Save | Issue |
|----------|-------------|-------------|-------|
| `SerialInit` | none | `a6` | Missing save of preserved A6 |
| `SerialPutChar` | `d0/a6` | `a6` | Oversaving scratch D0 |
| `SerialPutString` | `d0/a0/a6` | `a6` | Oversaving scratch D0, A0 |
| `SerialGetChar` | `a6` | `a6` | ✅ Correct |
| `SerialWaitChar` | `a6` | `a6` | ✅ Correct |
| `SerialPutHex8` | `d0-d2` | `d2` | Oversaving scratch D0, D1 |
| `SerialPutHex16` | `d0-d1` | none | Oversaving scratch D0, D1 |
| `SerialPutHex32` | `d0-d2/a0` | `d2` | Oversaving scratch D0, D1, A0 |
| `SerialPutDecimal` | `d0-d2/a0-a2` | `d2/a2` | Oversaving scratch D0, D1, A0, A1 |

## Changes Required

### 1. Global Label Rename (PascalCase → snake_case)

| Current | New |
|---------|-----|
| `SerialInit` | `serial_init` |
| `SerialPutChar` | `serial_put_char` |
| `SerialPutString` | `serial_put_string` |
| `SerialGetChar` | `serial_get_char` |
| `SerialWaitChar` | `serial_wait_char` |
| `SerialPutHex8` | `serial_put_hex8` |
| `SerialPutHex16` | `serial_put_hex16` |
| `SerialPutHex32` | `serial_put_hex32` |
| `SerialPutDecimal` | `serial_put_decimal` |

### 2. Register Convention Fixes

#### `serial_init` (line 13-16)
- **Current**: No save
- **Fix**: Add `movem.l a6,-(sp)` and `movem.l (sp)+,a6`

#### `serial_put_char` (line 23-31)
- **Current**: `movem.l d0/a6,-(sp)`
- **Fix**: `movem.l a6,-(sp)` - only save preserved A6

#### `serial_put_string` (line 38-47)
- **Current**: `movem.l d0/a0/a6,-(sp)`
- **Fix**: `movem.l a6,-(sp)` - only save preserved A6

#### `serial_get_char` (line 54-66)
- **Current**: `movem.l a6,-(sp)`
- **Status**: ✅ Already correct, no change needed

#### `serial_wait_char` (line 73-82)
- **Current**: `movem.l a6,-(sp)`
- **Status**: ✅ Already correct, no change needed

#### `serial_put_hex8` (line 89-119)
- **Current**: `movem.l d0-d2,-(sp)`
- **Fix**: `movem.l d2,-(sp)` - only save preserved D2

#### `serial_put_hex16` (line 126-136)
- **Current**: `movem.l d0-d1,-(sp)`
- **Fix**: Remove movem entirely - only uses scratch registers

#### `serial_put_hex32` (line 143-167)
- **Current**: `movem.l d0-d2/a0,-(sp)`
- **Fix**: `movem.l d2,-(sp)` - only save preserved D2

#### `serial_put_decimal` (line 174-219)
- **Current**: `movem.l d0-d2/a0-a2,-(sp)`
- **Fix**: `movem.l d2/a2,-(sp)` - only save preserved D2, A2

## Files Modified
- `src/rom/serial.s` - Label renames and register fixes
- `src/rom/bootstrap.s` - Update call to `serial_init`
- `src/rom/debugger.s` - Update calls to serial functions
- `src/rom/panic.s` - Update calls to serial functions
- `src/rom/memory.s` - Update calls to serial functions
- `src/rom/autoconfig.s` - Update calls to serial functions
- `src/rom/sprintf.s` - Update calls to serial functions (SerialPrintf)

## Implementation Order
1. Rename global labels in serial.s
2. Fix register save/restore in each function
3. Update all external references in other files
4. Build and test with `make rom`
5. Test with debug.py using temporary debugger entry

## Verification

### Testing Approach
Use the same approach as debugger.s refactor:
1. Temporarily modify `bootstrap.s` to jump to debugger before kernel boot
2. Run debug.py tests to verify serial output works

### Test Commands
```bash
# Build ROM
make clean && make rom

# Test serial output via debugger
echo -e "r\nm fc0000\nq\n" | ./debug.py 2>&1 | tail -40

# Verify register dump works (uses serial_put_hex32, serial_put_string)
# Verify memory dump works (uses serial_put_hex8/16/32, serial_put_char)
```

### Expected Outcomes
- Banner message displays correctly (serial_put_string)
- Register dump shows all registers in hex (serial_put_hex32)
- Memory dump shows bytes/words/longs (serial_put_hex8/16/32)
- Interactive input works (serial_wait_char)
- No functional changes, just cleaner register usage

## Risk Assessment
- **Low risk**: Label renames are straightforward find/replace across multiple files
- **Medium risk**: Register convention changes, especially SerialInit adding A6 save
- **Mitigation**:
  - Test thoroughly with debug.py after each change
  - SerialInit is only called once during boot, so impact is minimal
  - All other functions are simplifications (removing unnecessary saves)

## Benefits
- **Consistency**: Matches debugger.s refactor (snake_case labels)
- **Correctness**: SerialInit now properly saves A6 as a preserved register
- **Performance**: Reduced stack usage by removing unnecessary register saves
- **Code size**: Smaller code due to fewer movem instructions
- **Maintainability**: Clear indication of which registers need preservation
