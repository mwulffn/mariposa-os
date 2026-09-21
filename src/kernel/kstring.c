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
