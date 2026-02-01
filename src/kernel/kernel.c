/*
 * kernel.c - Minimal kernel
 */

#include "amiga_hw.h"

typedef struct {
    unsigned long base;
    unsigned long size;
    unsigned short type;
    unsigned short flags;
} MemEntry;

/* Memory types from ROM */
#define MEM_END      0
#define MEM_CHIP     1
#define MEM_FAST     2
#define MEM_ROM      5
#define MEM_RESERVED 6

/* ROM panic function - set by crt0 */
extern void (*rom_panic)(void);

void kernel_main(MemEntry *memmap)
{
    /* Purple background - proof that C code is running */
    custom.color[0] = RGB4(8,0,8);

    /* TODO: parse memory map */
    /* TODO: initialize memory allocator */
    /* TODO: set up exception handlers */
    /* TODO: initialize hardware */
    /* TODO: everything else */

    for (;;) {
        /* halt until interrupt (if interrupts were enabled) */
    }
}
