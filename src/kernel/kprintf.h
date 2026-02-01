/*
 * kprintf.h - Kernel printf
 */

#ifndef KPRINTF_H
#define KPRINTF_H

/* Log levels */
#define KL_EMERG   0   /* System is unusable */
#define KL_ERR     1   /* Error conditions */
#define KL_WARN    2   /* Warning conditions */
#define KL_INFO    3   /* Informational */
#define KL_DEBUG   4   /* Debug messages */

/* Current log level threshold - messages above this are suppressed */
extern int kprintf_level;

/*
 * Kernel printf with log level.
 * Returns number of characters printed.
 */
int kprintf(int level, const char *fmt, ...);

/*
 * Convenience macros
 */
#define pr_emerg(...) kprintf(KL_EMERG, __VA_ARGS__)
#define pr_err(...)   kprintf(KL_ERR,   __VA_ARGS__)
#define pr_warn(...)  kprintf(KL_WARN,  __VA_ARGS__)
#define pr_info(...)  kprintf(KL_INFO,  __VA_ARGS__)
#define pr_debug(...) kprintf(KL_DEBUG, __VA_ARGS__)

/*
 * sprintf - format to buffer
 * Returns number of characters written (excluding null terminator).
 */
int ksprintf(char *buf, const char *fmt, ...);

/*
 * snprintf - format to buffer with size limit
 */
int ksnprintf(char *buf, unsigned long size, const char *fmt, ...);

#endif /* KPRINTF_H */
