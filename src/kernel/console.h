/*
 * console.h - a command line on a character device
 */
#ifndef CONSOLE_H
#define CONSOLE_H

/* Start the console task on ser0. Returns 0, or -1 if there is no ser0 or
 * no memory for the task. */
int console_init(void);

#endif /* CONSOLE_H */
