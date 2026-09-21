/*
 * kstring.h - the string helpers the kernel has needed so far
 */
#ifndef KSTRING_H
#define KSTRING_H

/* Non-zero if the two strings are identical. */
int str_eq(const char *a, const char *b);

/* The same, ignoring case - ASCII and the Latin-1 letters both, since a
 * Danish file name should match however its ae was typed. */
int str_caseeq(const char *a, const char *b);

unsigned long str_len(const char *s);

#endif /* KSTRING_H */
