/*
 * kprintf.c - Kernel printf
 *
 * Minimal printf implementation for kernel use.
 * Supports: %d %i %u %x %X %p %s %c %%
 *           %l variants (ld, lu, lx, lX)
 *           width, zero-pad, left-align
 */

#include "kprintf.h"
#include "serial.h"
#include "stdarg.h"

extern void (*rom_panic)(void);


/* Default: show everything up to INFO */
int kprintf_level = KL_INFO;


/* Output context */
struct output {
    char *buf;           /* NULL for serial output */
    unsigned long size;  /* Buffer size (0 = unlimited) */
    unsigned long pos;   /* Current position */
};

static void out_char(struct output *o, char c)
{
    if (o->buf) {
        /* String output */
        if (o->size == 0 || o->pos < o->size - 1) {
            o->buf[o->pos] = c;
        }
    } else {
        /* Serial output */
        if (c == '\n')
            ser_putc('\r');
        ser_putc(c);
    }
    o->pos++;
}

static void out_string(struct output *o, const char *s)
{
    while (*s)
        out_char(o, *s++);
}

static void out_pad(struct output *o, int count, char c)
{
    while (count-- > 0)
        out_char(o, c);
}

/* Digit tables */
static const char digits_lower[] = "0123456789abcdef";
static const char digits_upper[] = "0123456789ABCDEF";

/* Convert unsigned long to string, return length */
static int ultoa(unsigned long val, char *buf, unsigned long base, int uppercase)
{
    const char *digits = uppercase ? digits_upper : digits_lower;
    char tmp[24];  /* Enough for 64-bit in binary */
    int i = 0;
    int len;
    unsigned long digit;

    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }

    while (val) {
        digit = val % base;
        tmp[i++] = digits[digit];
        val /= base;
    }

    len = i;
    while (i > 0)
        *buf++ = tmp[--i];
    *buf = '\0';

    return len;
}

/* Convert signed long to string, return length */
static int ltoa(long val, char *buf)
{
    if (val < 0) {
        *buf++ = '-';
        return 1 + ultoa((unsigned long)(-val), buf, 10, 0);
    }
    return ultoa((unsigned long)val, buf, 10, 0);
}

static int do_format(struct output *o, const char *fmt, va_list ap)
{
    char numbuf[24];
    const char *s;
    int width;
    int len;
    int left_align;
    int zero_pad;
    int is_long;
    unsigned long uval;
    long sval;
    char c;

    while ((c = *fmt++) != '\0') {
        if (c != '%') {
            out_char(o, c);
            continue;
        }

        /* Parse flags */
        left_align = 0;
        zero_pad = 0;
        width = 0;
        is_long = 0;

        /* Left align flag */
        if (*fmt == '-') {
            left_align = 1;
            fmt++;
        }

        /* Zero pad flag */
        if (*fmt == '0') {
            zero_pad = 1;
            fmt++;
        }

        /* Width */
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        /* Long modifier */
        if (*fmt == 'l') {
            is_long = 1;
            fmt++;
        }

        /* Conversion specifier */
        switch (*fmt++) {
        case 'd':
        case 'i':
            if (is_long)
                sval = va_arg(ap, long);
            else
                sval = va_arg(ap, int);
            len = ltoa(sval, numbuf);
            goto print_number;

        case 'u':
            if (is_long)
                uval = va_arg(ap, unsigned long);
            else
                uval = va_arg(ap, unsigned int);
            len = ultoa(uval, numbuf, 10, 0);
            goto print_number;

        case 'x':
            if (is_long)
                uval = va_arg(ap, unsigned long);
            else
                uval = va_arg(ap, unsigned int);
            len = ultoa(uval, numbuf, 16, 0);
            goto print_number;

        case 'X':
            if (is_long)
                uval = va_arg(ap, unsigned long);
            else
                uval = va_arg(ap, unsigned int);
            len = ultoa(uval, numbuf, 16, 1);
            goto print_number;

        case 'p':
            uval = (unsigned long)va_arg(ap, void *);
            out_char(o, '0');
            out_char(o, 'x');
            len = ultoa(uval, numbuf, 16, 0);
            /* Pointers: always pad to 8 digits */
            if (width == 0) width = 8;
            zero_pad = 1;
            goto print_number;

        print_number:
            if (!left_align && width > len)
                out_pad(o, width - len, zero_pad ? '0' : ' ');
            out_string(o, numbuf);
            if (left_align && width > len)
                out_pad(o, width - len, ' ');
            break;

        case 's':
            s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            len = 0;
            while (s[len]) len++;
            if (!left_align && width > len)
                out_pad(o, width - len, ' ');
            out_string(o, s);
            if (left_align && width > len)
                out_pad(o, width - len, ' ');
            break;

        case 'c':
            c = (char)va_arg(ap, int);
            if (!left_align && width > 1)
                out_pad(o, width - 1, ' ');
            out_char(o, c);
            if (left_align && width > 1)
                out_pad(o, width - 1, ' ');
            break;

        case '%':
            out_char(o, '%');
            break;

        default:
            /* Unknown specifier, print literally */
            out_char(o, '%');
            out_char(o, fmt[-1]);
            break;
        }
    }

    return (int)o->pos;
}

int kprintf(int level, const char *fmt, ...)
{
    struct output o;
    va_list ap;
    int ret;

    if (level > kprintf_level)
        return 0;

    o.buf = (char *)0;
    o.size = 0;
    o.pos = 0;

    va_start(ap, fmt);
    ret = do_format(&o, fmt, ap);
    va_end(ap);

    return ret;
}

int ksprintf(char *buf, const char *fmt, ...)
{
    struct output o;
    va_list ap;
    int ret;

    o.buf = buf;
    o.size = 0;  /* Unlimited */
    o.pos = 0;

    va_start(ap, fmt);
    ret = do_format(&o, fmt, ap);
    va_end(ap);

    buf[o.pos] = '\0';
    return ret;
}

int ksnprintf(char *buf, unsigned long size, const char *fmt, ...)
{
    struct output o;
    va_list ap;
    int ret;

    if (size == 0)
        return 0;

    o.buf = buf;
    o.size = size;
    o.pos = 0;

    va_start(ap, fmt);
    ret = do_format(&o, fmt, ap);
    va_end(ap);

    /* Null terminate */
    if (o.pos < size)
        buf[o.pos] = '\0';
    else
        buf[size - 1] = '\0';

    return ret;
}
