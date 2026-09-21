/*
 * kstring.c - the string helpers the kernel has needed so far
 */
#include "kstring.h"

int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static unsigned char fold(unsigned char c)
{
    if (c >= 'A' && c <= 'Z')
        return (unsigned char)(c + 32);
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7)    /* Latin-1 capitals, not the x sign */
        return (unsigned char)(c + 32);
    return c;
}

int str_caseeq(const char *a, const char *b)
{
    while (*a && fold((unsigned char)*a) == fold((unsigned char)*b)) {
        a++;
        b++;
    }
    return fold((unsigned char)*a) == fold((unsigned char)*b);
}

unsigned long str_len(const char *s)
{
    const char *p = s;

    while (*p)
        p++;
    return (unsigned long)(p - s);
}
