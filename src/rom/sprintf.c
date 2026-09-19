/*
 * sprintf.c - formatted output for the ROM
 *
 * Replaces the 366-line sprintf.s. The behaviour is the one documented in
 * docs/rom/sprintf_api.md and pinned by the format.* tests; this is a
 * transcription, not a redesign.
 *
 * Arguments arrive as an array of longword slots rather than through
 * <stdarg.h>: the ~40 call sites in the ROM's assembly push arguments and
 * `bsr SerialPrintf`, and sprintf_glue.s turns that frame into a pointer.
 * That keeps the ABI exactly where it was and leaves this file free of any
 * dependency on how vbcc happens to implement varargs.
 *
 * No writable statics: this lives in ROM. The only mutable state is the
 * caller-visible buffer in chip RAM.
 */

/* Scratch buffer in chip RAM, mirroring SPRINTF_BUFFER in hardware.i. */
#define SPRINTF_BUFFER ((char *)0x3400)

typedef unsigned long u32;

/* Size modifiers. Long unless a .b or .w says otherwise. */
#define SZ_BYTE 'b'
#define SZ_WORD 'w'
#define SZ_LONG 'l'

static int is_size_letter(char c)
{
    return c == SZ_BYTE || c == SZ_WORD || c == SZ_LONG;
}

/* Narrow a longword slot to the requested width. Callers always push a full
 * long whatever width they asked to display, so this masks rather than reads
 * short - reading a word would take the high half of the slot. */
static u32 narrow(u32 v, char size)
{
    if (size == SZ_BYTE) return v & 0xFFul;
    if (size == SZ_WORD) return v & 0xFFFFul;
    return v;
}

static char *put_hex(char *out, u32 value, int digits)
{
    int shift = (digits - 1) * 4;

    for (; digits > 0; digits--, shift -= 4) {
        unsigned nibble = (unsigned)((value >> shift) & 0xF);
        *out++ = (char)(nibble < 10 ? '0' + nibble : 'A' - 10 + nibble);
    }
    return out;
}

static char *put_bin(char *out, u32 value, int bits)
{
    int shift = bits - 1;

    for (; bits > 0; bits--, shift--)
        *out++ = (char)('0' + (unsigned)((value >> shift) & 1));
    return out;
}

/* Unsigned, full 32-bit range. Digits come out backwards and are reversed in
 * place, which is what the assembly did too. */
static char *put_dec(char *out, u32 value)
{
    char *start = out;
    char *end;

    if (value == 0) {
        *out++ = '0';
        return out;
    }

    while (value != 0) {
        *out++ = (char)('0' + (unsigned)(value % 10));
        value /= 10;
    }

    for (end = out - 1; start < end; start++, end--) {
        char t = *start;
        *start = *end;
        *end = t;
    }
    return out;
}

/*
 * Format into SPRINTF_BUFFER and return the length. `args` points at the
 * first argument slot; every specifier that takes one consumes exactly one
 * longword, whatever width it displays.
 */
u32 rom_vsprintf(const char *fmt, const u32 *args)
{
    char *out = SPRINTF_BUFFER;
    char c;

    while ((c = *fmt++) != '\0') {
        int width;
        char size;

        if (c != '%') {
            *out++ = c;
            continue;
        }

        /* Optional width, so "%08x" works and not just "%8x". A leading zero
         * is decoration - hex always emits exactly the digit count it is
         * given, zero filled. Clamped to 8: a 32-bit value is 8 digits. */
        width = 0;
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');
        if (width > 8)
            width = 8;

        /* Size ahead of the specifier: "%.lx". Only consume the '.' when a
         * size letter really follows, so a stray dot stays literal text. */
        size = SZ_LONG;
        if (fmt[0] == '.' && is_size_letter(fmt[1])) {
            size = fmt[1];
            fmt += 2;
        }

        c = *fmt;
        if (c == '\0')
            break;
        fmt++;

        if (c == '%') {
            *out++ = '%';
            continue;
        }

        /* Size after the specifier: "%x.l", the form almost every caller in
         * this ROM writes. Gated on specifiers where a size means something,
         * or "%s.bin" would swallow the ".b" and print "in", and "%d.log"
         * would lose its ".l" - a trap for whoever writes the next format
         * string. */
        if ((c == 'x' || c == 'b') && fmt[0] == '.' && is_size_letter(fmt[1])) {
            size = fmt[1];
            fmt += 2;
        }

        switch (c) {
        case 'x': {
            int digits = (size == SZ_BYTE) ? 2 : (size == SZ_WORD) ? 4 : 8;
            if (width != 0)
                digits = width;                  /* width overrides size */
            out = put_hex(out, narrow(*args++, size), digits);
            break;
        }
        case 'd':
            out = put_dec(out, *args++);
            break;
        case 'b': {
            int bits = (size == SZ_BYTE) ? 8 : (size == SZ_WORD) ? 16 : 32;
            out = put_bin(out, narrow(*args++, size), bits);
            break;
        }
        case 's': {
            const char *s = (const char *)*args++;
            while (*s != '\0')
                *out++ = *s++;
            break;
        }
        default:
            /* Unknown specifier: swallowed, exactly as the assembly did. */
            break;
        }
    }

    *out = '\0';
    return (u32)(out - SPRINTF_BUFFER);
}
