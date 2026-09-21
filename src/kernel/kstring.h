/*
 * kstring.h - the string helpers the kernel has needed so far
 */
#ifndef KSTRING_H
#define KSTRING_H

/* Non-zero if the two strings are identical. */
int str_eq(const char *a, const char *b);

#endif /* KSTRING_H */
