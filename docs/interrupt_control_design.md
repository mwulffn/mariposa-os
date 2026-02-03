# Interrupt Control Design

## Overview

Interrupt enable/disable must be atomic and safe to use from any context. On the 68000, modifying SR is a single instruction and inherently atomic.

## Rules

1. Never use blind enable/disable pairs. Always save and restore SR.
2. Every function that touches shared state must use CRITICAL_ENTER/CRITICAL_EXIT.
3. ISR code runs with interrupts already disabled by the 68000. No need to disable again.

## Implementation

Assembly file (cpu.s) provides four functions:

```
_cpu_sr_get      → returns current SR in D0
_cpu_sr_set      ← takes SR value in D0, writes to SR
_cpu_int_disable → writes $2700 to SR
_cpu_int_enable  → writes $2000 to SR
```

These are thin wrappers around single instructions. The overhead of a JSR is ~18 cycles. Acceptable for now.

## C Interface

```
unsigned short cpu_sr_get(void);
void cpu_sr_set(unsigned short sr);
void cpu_int_disable(void);
void cpu_int_enable(void);
```

## Critical Section Macros

```
CRITICAL_ENTER(save)   → save = cpu_sr_get(); cpu_int_disable();
CRITICAL_EXIT(save)    → cpu_sr_set(save);
```

Usage:

```
unsigned short saved;
CRITICAL_ENTER(saved);
/* ... shared state access ... */
CRITICAL_EXIT(saved);
```

CRITICAL_EXIT restores whatever interrupt state the caller had. This makes critical sections safe to nest and safe to call from ISR context.

## Where Critical Sections Are Needed

- Serial ring buffer (ser_putc)
- Task queue manipulation (wake, enqueue, dequeue)
- Memory allocator (both heaps)
- Any global state modified by both task and ISR context

## Where They Are Not Needed

- ISR code (already running with interrupts masked by 68000)
- Single-writer data only touched by one context
- Local variables

## Future: Inline Option

When compiler-specific inline assembly is validated, the four functions can be replaced with static inline versions in a header. The C interface and macros remain identical. Only the implementation changes from JSR to inline.
