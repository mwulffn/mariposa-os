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

/* Scratch for test data (strings, buffers), bump-allocated, reset per test. */
#define H_SCRATCH_BASE 0x280000u
#define H_SCRATCH_SIZE 0x010000u

/* Guest stack top. Grows down, well clear of the scratch area. */
#define H_STACK_TOP    0x2F0000u

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

/* --- symbols ------------------------------------------------------------ */

/* Address of a ROM label or equate, by name, as emitted by tests/mksym.py.
 * An unknown name is a harness error: it aborts with a clear message rather
 * than letting a test silently run against address zero. */
uint32_t h_sym(const char *name);

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

/* Cycle budget for a single call. Generous by default; lower it in a test
 * that is specifically checking something terminates. */
void h_set_cycle_budget(uint64_t cycles);

/* --- serial capture ----------------------------------------------------- */

/* Everything the guest has written to SERDAT since the last h_reset. */
const char *h_serial(void);
size_t      h_serial_len(void);

/* Queue input for serial_get_char / serial_wait_char.
 *
 * Deviation from real hardware, deliberate: the model clears RBF when the
 * guest reads SERDATR as a word, whereas Paula requires an INTREQ write to
 * ack. Without this the ROM's serial_get_char, which never acks, would spin
 * forever. Any test of the ack path itself has to check INTREQ directly.
 */
void h_serial_input(const char *s);

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
void     h_detach_disk(void);
uint32_t h_disk_sectors(void);

/* Read a sector straight out of the host-side image, for building the
 * expectation a test compares the guest's result against. */
int      h_disk_read(uint32_t lba, void *buf512);

#endif /* HARNESS_H */
