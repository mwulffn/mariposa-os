/*
 * font8x8.h - the boot console's font
 */
#ifndef FONT8X8_H
#define FONT8X8_H

/* Indexed by Latin-1 code point. One byte per row, leftmost pixel in bit 7. */
extern const unsigned char font8x8[256][8];

#endif /* FONT8X8_H */
