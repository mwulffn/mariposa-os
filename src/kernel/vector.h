/*
 * vector.h - the exception vector table, wherever it is
 */
#ifndef VECTOR_H
#define VECTOR_H

/* CPU_* from bootinfo.h. Set once, early, from the handoff; a 68000 until
 * then. Decides where the vector table is and what an exception frame looks
 * like - see docs/task_design.md. */
extern unsigned long cpu_type;

#define VEC_AUTOVECTOR(level)  (24 + (level))
#define VEC_TRAP(n)            (32 + (n))

/* Install a handler. Every install goes through here: the table is at
 * address 0 on a 68000 and wherever VBR points on anything later. */
void **vector_table(void);
void  vector_set(unsigned int vector, void (*handler)(void));
void *vector_get(unsigned int vector);

#endif /* VECTOR_H */
