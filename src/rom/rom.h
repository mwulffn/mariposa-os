/*
 * rom.h - internal interface between the ROM's C modules
 *
 * Not an API for the kernel: that is still a single panic vector. These are
 * the entry points ROM C files call in each other, and the addresses of the
 * fixed chip-RAM scratch areas that hardware.i also names for the assembly.
 */
#ifndef ROM_H
#define ROM_H

/* Sprintf output buffer, mirroring SPRINTF_BUFFER in hardware.i. */
#define ROM_SPRINTF_BUFFER ((char *)0x3400)

/* sprintf.c */
unsigned long rom_vsprintf(const char *fmt, const unsigned long *args);

/* Format and write to the serial port. Arguments are longword slots, the
 * same shape the assembly call sites push - see sprintf_glue.s. */
void rom_printf(const char *fmt, const unsigned long *args);

/* serial.c */
void rom_serial_init(void);
void rom_serial_put_char(unsigned long c);
void rom_serial_put_string(const char *s);
unsigned long rom_serial_get_char(void);
unsigned long rom_serial_wait_char(void);
void rom_serial_put_hex(unsigned long value, unsigned long digits);
void rom_serial_put_decimal(unsigned long value);

#endif /* ROM_H */
