/*
 * harness.h - headless 68000 test harness
 *
 * Runs routines out of the real kick.rom image under the Musashi 68000 core,
 * with no emulator, no display and no serial port. A call takes microseconds,
 * so ROM assembly can be unit tested the way C is.
 *
 * The machine model is deliberately small: RAM, the ROM image, and just
 * enough of Paula's UART that serial output can be captured. Anything a
 * routine touches that is not modelled is recorded as a fault and fails the
 * test, rather than silently reading zero.
 */
#ifndef HARNESS_H
#define HARNESS_H

#include <stddef.h>
#include <stdint.h>

/* --- memory map ---------------------------------------------------------
 * Matches src/rom/hardware.i where it matters. Addresses outside these
 * regions are faults.
 */
#define H_CHIP_BASE   0x000000u
#define H_CHIP_SIZE   0x200000u          /* 2MB, so chip RAM sizing probes work */
#define H_FAST_BASE   0x200000u
#define H_FAST_SIZE   0x100000u          /* 1MB, holds KERNEL_LOAD_ADDR */
#define H_ROM_BASE    0xFC0000u
#define H_ROM_SIZE    0x040000u          /* 256KB */

/* Trapdoor RAM at $C00000 ("slow", "ranger", "bogo"). Absent by default.
 * H_SLOW_LIMIT is where Gary stops decoding it and Gayle, the battery clock
 * and the custom chips begin - detect_slow_ram must never probe that far. */
#define H_SLOW_BASE   0x00C00000u
#define H_SLOW_LIMIT  0x00D80000u

/* Scratch for test data (strings, buffers), bump-allocated, reset per test. */
#define H_SCRATCH_BASE 0x280000u
#define H_SCRATCH_SIZE 0x010000u

/* Guest stack top. Grows down, well clear of the scratch area. */
#define H_STACK_TOP    0x2F0000u

/* Zorro II expansion space above H_FAST. An unpopulated bus floats high:
 * reads return all ones, writes go nowhere. detect_fast_ram sizes memory by
 * probing past the end and failing the read-back, so this is what stops it
 * without the probe looking like a fault. */
#define H_ZORRO_END    0x00A00000u

/* Sentinels. Never mapped: the run loop checks PC before each instruction,
 * so these are recognised without ever being fetched from. */
#define H_RETURN_ADDR  0x00F00000u       /* a routine returning here is done */
#define H_VECTOR_TRAP  0x00E00000u       /* exception vectors point here + n*4 */

typedef enum {
    H_OK = 0,        /* routine returned to H_RETURN_ADDR */
    H_TIMEOUT,       /* cycle budget exhausted - runaway or infinite loop */
    H_EXCEPTION,     /* took a 68000 exception */
    H_FAULT          /* touched unmapped memory, wrote ROM, or misaligned */
} h_status;

typedef struct {
    h_status status;
    int      vector;          /* exception vector number, if H_EXCEPTION */
    uint64_t cycles;
    char     detail[256];     /* one line, human readable, no newlines */
} h_result;

/* --- lifecycle ---------------------------------------------------------- */

/* Load the ROM image and the symbol file. Returns 0 on success; on failure
 * prints the reason and returns non-zero (this is a harness error, not a
 * test failure). */
int  h_init(const char *rom_path, const char *sym_path);
void h_shutdown(void);

/* Clear RAM, reset the CPU, arm the exception traps, empty the serial
 * capture and the scratch allocator. Call before every test. */
void h_reset(void);

/* --- extra code modules --------------------------------------------------
 *
 * Load a position-independent blob at a fixed address so its routines can be
 * called like ROM ones. This is how the kernel's libsup.s gets tested: it is
 * assembled standalone to origin zero, loaded here, and its symbols merged
 * with a matching bias.
 *
 * The blob is re-applied after every h_reset(), since reset clears RAM.
 * Fails loudly rather than silently if the file is missing.
 */
int h_load_module(const char *path, uint32_t addr);

/* --- symbols ------------------------------------------------------------ */

/* Address of a ROM label or equate, by name, as emitted by tests/mksym.py.
 * An unknown name is a harness error: it aborts with a clear message rather
 * than letting a test silently run against address zero. */
uint32_t h_sym(const char *name);

/* Merge a second symbol file, adding `bias` to every value. Use it for a
 * module assembled at origin zero and loaded somewhere else. */
int h_add_symbols(const char *path, uint32_t bias);

/* The same, with `prefix` glued onto every name. The kernel image shares
 * source with the ROM (serial_hw.c, for one), so its symbols would collide
 * with the ROM's and lose: lookup is first match. Loaded as "kernel:", a
 * test says h_sym("kernel:_ser_puts") and gets the kernel's. */
int h_add_symbols_prefixed(const char *path, uint32_t bias, const char *prefix);

/* --- guest memory ------------------------------------------------------- */

uint32_t h_alloc(const void *data, size_t len);  /* copy into scratch, return address */
uint32_t h_str(const char *s);                   /* NUL-terminated string into scratch */

uint8_t  h_peek8(uint32_t addr);
uint16_t h_peek16(uint32_t addr);
uint32_t h_peek32(uint32_t addr);
void     h_poke8(uint32_t addr, uint8_t v);
void     h_poke16(uint32_t addr, uint16_t v);
void     h_poke32(uint32_t addr, uint32_t v);

/* Read a NUL-terminated guest string into buf. Always NUL-terminates. */
void     h_peekstr(uint32_t addr, char *buf, size_t bufsz);

/* --- registers ---------------------------------------------------------- */

void     h_set_d(int n, uint32_t v);   /* n = 0..7 */
void     h_set_a(int n, uint32_t v);   /* n = 0..6 */
uint32_t h_get_d(int n);
uint32_t h_get_a(int n);

/* Set the stack pointer directly. Needed when a test builds an exception
 * frame by hand instead of calling a routine. */
void     h_set_sp(uint32_t v);
uint32_t h_get_sp(void);

/* --- calling a routine -------------------------------------------------- */

/* Reset SP to H_STACK_TOP. Call before pushing arguments. */
void h_begin_call(void);

/* Push a longword. The ROM's printf-style routines take arguments on the
 * stack pushed right to left, with the format string pushed last. */
void h_push32(uint32_t v);

/* Push the return sentinel and run from `pc` until the routine returns,
 * faults, or burns through the cycle budget. */
h_result h_call(uint32_t pc);

/* Run from `pc` without pushing a return sentinel: for code that never
 * returns, such as an exception handler or panic. Set up the stack yourself
 * first. Such a run ends in H_TIMEOUT by design - inspect h_serial() and
 * guest memory for the result rather than the status. */
h_result h_run(uint32_t pc);

/* The base of `size` bytes of fast RAM that are clear of the kernel image
 * loaded at $200000 - its .bss included - and of the harness's own scratch
 * area and stacks. Where a test puts a heap for the kernel's allocator.
 * Never a constant: the kernel grows. A harness error if it does not fit. */
uint32_t h_kernel_heap(uint32_t size);

/* Carry on from wherever the last h_run stopped, for another cycle budget.
 * For code that never returns and is observed in slices - a scheduler. */
h_result h_resume(void);
uint32_t h_get_pc(void);

/* Cycle budget for a single call. Generous by default; lower it in a test
 * that is specifically checking something terminates. */
void h_set_cycle_budget(uint64_t cycles);

/* --- serial capture ----------------------------------------------------- */

/* Everything the guest has written to SERDAT since the last h_reset or
 * h_serial_clear(). */
const char *h_serial(void);
size_t      h_serial_len(void);

/* Drop the captured output. A test that calls a printing routine more than
 * once needs this between calls, or it reads back all of them concatenated. */
void        h_serial_clear(void);

/* How many times SERDAT was written while TBE was clear. On the real chip
 * each of those destroys the byte still waiting in the buffer, so for any
 * driver the only acceptable answer is zero. The capture above cannot show
 * it - it records every write - which is why this is counted separately. */
unsigned    h_serial_overruns(void);

/* Transmitter speed, in CPU cycles from the SERDAT write: when the buffer
 * frees (TBE) and when the shifter empties (TSRE). The default 64/128 is far
 * faster than any real baud rate, so a ring buffer never fills; a test that
 * wants it to fill slows the UART down. 9600 baud is about 7400 cycles a
 * character. Restored by h_reset. */
void        h_serial_set_timing(uint64_t tbe_cycles, uint64_t tsre_cycles);

/* Queue input for serial_get_char / serial_wait_char.
 *
 * RBF is modelled the way Paula behaves: it mirrors INTREQ bit 11, reading
 * SERDATR does not clear it, and the next byte only latches once software
 * acknowledges by writing INTREQ. Code that reads without acknowledging sees
 * the same character for ever.
 *
 * This used to be faked - the model cleared RBF on a word read - which hid
 * the fact that the ROM's receive path never acknowledged. Verified against
 * FS-UAE: custom.cpp reads SERDATR with no side effect, and serial.cpp keeps
 * serdat at 0x4100|byte until the next byte arrives.
 */
void h_serial_input(const char *s);

/* How fast queued input arrives. 0, the default, is a sender that waits for
 * the receiver: the next byte latches the moment the last is acknowledged.
 * Non-zero is a real line: a byte every `cycles` cycles regardless, and one
 * arriving while the last is still unacknowledged is lost and sets SERDATR's
 * OVRUN bit until RBF is cleared. 9600 baud is about 7400. Call before
 * h_serial_input. Reset by h_reset. */
void h_serial_rx_pacing(uint64_t cycles);

/* --- interrupts ----------------------------------------------------------
 *
 * Paula's interrupt registers and the 68000 IPL lines, modelled properly
 * enough to test autovector dispatch on a real 68000 core.
 *
 * The UART sits on top of these: RBF (bit 11) and TBE (bit 0) are interrupt
 * bits before they are status bits, so the serial model above and this one
 * are the same mechanism seen from two sides.
 *
 * INTENA ($DFF09A) and INTREQ ($DFF09C) are write-only SET/CLR registers:
 * bit 15 set means "set the bits I name", bit 15 clear means "clear them".
 * They read back through INTENAR ($DFF01C) and INTREQR ($DFF01E). Bit 14 of
 * INTENA is the master enable; nothing reaches the CPU without it.
 *
 * The level presented to the CPU is recomputed on every write to either
 * register, which gives real Amiga semantics for free: an interrupt stays
 * asserted until the handler clears its INTREQ bit, so a handler that
 * forgets to ack will be re-entered the instant it RTEs. That is a bug worth
 * catching, so the model does not paper over it.
 *
 * Level 7 is the NMI line and does not come from Paula. h_irq_force() drives
 * the IPL lines directly for tests that need it.
 */

/* INTENA/INTREQ bit numbers, and the level each one raises. */
#define H_INTB_TBE      0       /* level 1 - serial transmit buffer empty */
#define H_INTB_DSKBLK   1       /* level 1 */
#define H_INTB_SOFTINT  2       /* level 1 */
#define H_INTB_PORTS    3       /* level 2 - CIA-A, the keyboard lives here */
#define H_INTB_COPER    4       /* level 3 */
#define H_INTB_VERTB    5       /* level 3 - vertical blank */
#define H_INTB_BLIT     6       /* level 3 */
#define H_INTB_AUD0     7       /* level 4 */
#define H_INTB_AUD1     8       /* level 4 */
#define H_INTB_AUD2     9       /* level 4 */
#define H_INTB_AUD3     10      /* level 4 */
#define H_INTB_RBF      11      /* level 5 - serial receive buffer full */
#define H_INTB_DSKSYN   12      /* level 5 */
#define H_INTB_EXTER    13      /* level 6 - CIA-B */
#define H_INTB_INTEN    14      /* master enable, in INTENA only */
#define H_INTB_SETCLR   15      /* write direction */

#define H_INTF(b)       ((uint16_t)(1u << (b)))

#define H_INTF_TBE      H_INTF(H_INTB_TBE)
#define H_INTF_PORTS    H_INTF(H_INTB_PORTS)
#define H_INTF_VERTB    H_INTF(H_INTB_VERTB)
#define H_INTF_RBF      H_INTF(H_INTB_RBF)
#define H_INTF_EXTER    H_INTF(H_INTB_EXTER)
#define H_INTF_INTEN    H_INTF(H_INTB_INTEN)
#define H_INTF_SETCLR   H_INTF(H_INTB_SETCLR)

/* Current register contents, as INTENAR/INTREQR would read them. */
uint16_t h_intena(void);
uint16_t h_intreq(void);

/* Write INTENA/INTREQ from the host side, with the same SET/CLR encoding the
 * guest uses - so h_write_intena(H_INTF_SETCLR | H_INTF_INTEN | H_INTF_VERTB)
 * enables the master bit and VERTB together. */
void h_write_intena(uint16_t val);
void h_write_intreq(uint16_t val);

/* Assert an interrupt source, the way Paula would. Shorthand for
 * h_write_intreq(H_INTF_SETCLR | bits). */
void h_raise(uint16_t bits);

/* The level currently presented to the CPU: 0 when nothing is pending,
 * masked, or the master enable is off. */
int h_irq_level(void);

/* Drive the IPL lines directly, bypassing Paula. For level 7, and for tests
 * that want a level without inventing a source for it. Cleared by h_reset
 * and overridden by the next INTENA/INTREQ write. */
void h_irq_force(int level);

/* --- custom chip registers, and the copper ---------------------------------
 *
 * The chipset's write-only registers do nothing here, but every write is
 * remembered: h_custom(reg) is the last value written to the register at
 * that offset from $DFF000, h_custom_writes(reg) how many times it has been
 * written since h_reset. A long write counts as the two registers it is.
 *
 * h_copper_at() is a copper: it runs the list at `list` from the top of the
 * frame down to raster line `line` and fills regs[] - indexed by register
 * offset / 2 - with what each register holds there. MOVE and WAIT, vertical
 * position only, and the $FFDF idiom for lines past 255. Returns -1 if the
 * list never ends. That is what lets a test ask "which bitmap row is on
 * screen at line 140, in which colours" instead of comparing list bytes. */
uint16_t h_custom(uint32_t reg);
unsigned h_custom_writes(uint32_t reg);
int      h_copper_at(uint32_t list, int line, uint16_t regs[0x100]);

/* --- keyboard --------------------------------------------------------------
 *
 * A keyboard on CIA-A's serial port. h_key() queues a raw code - bit 7 set
 * for key up - and the model sends it when the last one has been
 * handshaken: SDR loaded in wire format (rotated and inverted), the SP flag
 * raised in ICR, PORTS raised in Paula if the CIA's mask allows.
 *
 * The handshake is the computer pulling KDAT low (CRA SPMODE to output) for
 * at least 85 microseconds. The model times the pulse: h_kbd_handshakes()
 * counts good ones and h_kbd_short_handshakes() ones that were too brief,
 * which is what a delay loop tuned on a slow CPU becomes on a fast one.
 *
 * ICR is modelled faithfully: reading it clears every flag on the chip,
 * and PORTS cannot be cleared while the CIA still asserts. Timer A counts
 * the E clock, one tick per ten CPU cycles. */
void     h_key(uint8_t code);
unsigned h_kbd_handshakes(void);
unsigned h_kbd_short_handshakes(void);
int      h_kbd_idle(void);              /* everything sent and handshaken */

/* Raise VERTB every `cycles` CPU cycles, 0 to stop. Off after h_reset. The
 * period is not 50Hz and is not meant to be: a scheduler test wants many
 * ticks inside its cycle budget, landing wherever they land. */
void h_vbl_every(uint64_t cycles);

/* Swap the CPU core: 68000, 68010, 68020, 68030 or 68040 (the last three as
 * their EC variants, which keep the 24-bit bus this machine model has).
 * Resets the CPU. h_reset always goes back to the 68000, so a test that
 * wants another core says so after it, and cannot leak it to the next test.
 * What this buys is the exception frame: six bytes on a 68000, eight with a
 * format word on everything later. */
void h_set_cpu(int model);

/* The 68000 status register. The interrupt mask is bits 8-10: h_reset leaves
 * SR at $2700 (all interrupts masked, as ROM code runs), so a test that wants
 * an interrupt delivered has to lower it - h_set_sr(0x2000) is the usual
 * choice. */
void     h_set_sr(uint16_t sr);
uint16_t h_get_sr(void);

/* --- Zorro II autoconfig -------------------------------------------------
 *
 * One card slot. With nothing attached the space reads $FF, which is the
 * "no card" answer, so tests that do not care are unaffected.
 *
 * er_Type bits 7-6 must be %11 for a Zorro II card and bits 2-0 carry the
 * size code; er_Flags bit 7 marks a memory board. The card stops answering
 * once it is relocated or shut up, exactly as on the bus.
 */
void     h_attach_zorro(uint8_t er_type, uint8_t er_flags);
void     h_detach_zorro(void);

/* --- trapdoor / slow RAM -------------------------------------------------
 *
 * Nothing is fitted unless a test asks for it, so the memory map every other
 * test checks is the one it always was. Unfitted address ranges inside the
 * region float rather than faulting, which is what detect_slow_ram relies on.
 *
 * The mirrored form models a board that decodes fewer address lines than it
 * answers to: `size` bytes of real memory appearing over and over across
 * `decode` bytes of address space. Sizing that only wrote and read back
 * would measure `decode` and be wrong by up to 1MB. */
void     h_attach_slow_ram(uint32_t size);
void     h_attach_slow_ram_mirrored(uint32_t size, uint32_t decode);
void     h_detach_slow_ram(void);

/* The address the card was told to move to, or 0 if it never was. */
uint32_t h_zorro_base(void);
int      h_zorro_configured(void);
int      h_zorro_shut_up(void);

/* --- IDE disk ------------------------------------------------------------
 *
 * A Gayle-mapped ATA register model over a raw image file, enough for the
 * READ SECTORS path that ide.s implements: LBA28, PIO, no interrupts, no
 * DMA, BSY never asserted.
 *
 * With no image attached the status register reads $7F, which is what ide.s
 * treats as "no drive" - so tests that do not care about disks are
 * unaffected, and none of them hang.
 *
 * A word read of the data port returns the two bytes in disk order, high
 * byte first, so that a `move.w IDE_DATA,(a0)+` leaves memory holding the
 * sector byte for byte. That is what makes both the big-endian RDB compare
 * and the little-endian FAT parsing work off the same buffer.
 */

/* Returns 0 on success. Attaching replaces any previous image. Note h_reset()
 * detaches, so attach inside the test, after reset. */
int      h_attach_disk(const char *path);

/* READ SECTORS, WRITE SECTORS, IDENTIFY DEVICE and FLUSH CACHE are modelled.
 * Writes go to the in-memory copy of the image, never to the file, so a
 * test cannot damage the image the next one reads. These count what the
 * guest has asked of the drive since the image was attached - the only way
 * to see a cache working is to see the commands it did not send. */
unsigned h_disk_commands(void);
unsigned h_disk_sectors_read(void);
unsigned h_disk_sectors_written(void);
void     h_detach_disk(void);
uint32_t h_disk_sectors(void);

/* Read a sector straight out of the host-side image, for building the
 * expectation a test compares the guest's result against. */
int      h_disk_read(uint32_t lba, void *buf512);

#endif /* HARNESS_H */
