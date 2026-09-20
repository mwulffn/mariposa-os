/*
 * vector.c - the exception vector table, wherever it is
 */
#include "vector.h"
#include "cpu.h"
#include "bootinfo.h"

unsigned long cpu_type = CPU_68000;

void **vector_table(void)
{
    /* MOVEC is an illegal instruction on a 68000, so it is only asked of a
     * CPU that has a VBR to read. */
    if (cpu_type >= CPU_68010)
        return (void **)cpu_vbr_get();
    return (void **)0;
}

void vector_set(unsigned int vector, void (*handler)(void))
{
    vector_table()[vector] = (void *)handler;
}

void *vector_get(unsigned int vector)
{
    return vector_table()[vector];
}
