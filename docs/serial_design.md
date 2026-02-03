# Serial Subsystem Design

## Overview

Kernel serial is transmit-only, interrupt-driven. A 1024-byte ring buffer decouples kprintf callers from the 9600 baud wire speed. On crash, the kernel drops to the ROM debugger which register-bangs the UART directly, bypassing the ring buffer and interrupts entirely.

## Hardware

- Paula UART at $DFF000
- 9600 baud, 8N1
- TBE (Transmit Buffer Empty) interrupt at level 1, vector $64

## Normal Mode: Interrupt-Driven Transmit

### Data Structures

- 1024-byte ring buffer in fast RAM
- Head index: advanced by ser_putc (producer)
- Tail index: advanced by ISR (consumer)

### ser_putc

1. Expand `\n` to `\r\n`
2. Spin-wait if buffer full (should never happen with 1024 bytes at 9600 baud)
3. Save SR, disable interrupts
4. Write byte to buffer, advance head
5. Enable TBE interrupt
6. Restore SR

SR is saved and restored rather than blindly enabling interrupts. This ensures ser_putc is safe to call from critical sections where interrupts are already disabled.

### TBE ISR (Level 1)

1. Acknowledge TBE interrupt
2. If buffer not empty: write next byte to SERDAT, advance tail
3. If buffer empty: disable TBE interrupt

### Flow

```
kprintf → ser_putc → buffer[head++], enable TBE
                          │
              TBE fires ──┘
                  │
                  ▼
              SERDAT = buffer[tail++]
              TBE fires again when byte transmitted
                  │
                  ▼
              ... repeats until buffer empty ...
                  │
                  ▼
              Disable TBE interrupt
```

### Buffer Sizing

At 9600 baud, one byte takes ~1.04ms to transmit. The TBE ISR drains one byte per interrupt. To fill the 1024-byte buffer, the kernel would need to produce 1024 bytes before the ISR drains the first byte. In practice this cannot happen.

## Crash Mode: ROM Debugger

On panic, the kernel disables all interrupts and jumps to the ROM debugger. The ROM debugger polls the UART directly:

- Waits for TBE flag in SERDATR
- Writes directly to SERDAT
- No ring buffer, no interrupts, no dependencies on kernel state

The two paths share no state. The ROM debugger works regardless of how corrupted the kernel is.

## No Receive

The kernel does not receive serial data in normal operation. Serial receive is handled exclusively by the ROM debugger in crash mode.

## Initialization

ser_init sets SERPER for 9600 baud and clears the ring buffer. TBE interrupt is not enabled until the first byte is queued.
