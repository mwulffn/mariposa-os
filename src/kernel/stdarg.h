#ifndef _STDARG_H
#define _STDARG_H

typedef unsigned char *va_list;

/* align to 16-bit stack word, but round up to 32-bit when needed */
#define __va_align(type) \
    ((sizeof(type) + sizeof(int) - 1) & ~(sizeof(int) - 1))

#define va_start(ap, last) \
    (ap = (va_list)(&(last) + 1))

#define va_arg(ap, type) \
    (*(type *)((ap += __va_align(type)) - __va_align(type)))

#define va_end(ap) \
    ((void)(ap = (va_list)0))

#endif