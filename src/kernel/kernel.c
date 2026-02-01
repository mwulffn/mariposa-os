/*
 * kernel.c - Minimal kernel
 */

#include "amiga_hw.h"
#include "mem.h"
#include "serial.h"
#include "kprintf.h"

/* Linker symbols (vbcc adds underscore, so _end becomes __end) */
extern char _end;        /* Will become __end in assembly (matches linker script) */
extern char _bss_start;  /* Will become __bss_start in assembly */
extern char _bss_end;    /* Will become __bss_end in assembly */

/* ROM panic function - set by crt0 */
extern void (*rom_panic)(void);

void kernel_main(MemEntry *memmap)
{
    /* Purple background */
    custom.color[0] = RGB4(8,0,8);

    /* Initialize serial */
    ser_init();

    pr_info("\n");
    pr_info("Kernel starting successfully!\n");

    /* Initialize memory allocator */
    mem_init(memmap, &_end);

    pr_info("Memory system initialized\n");
    pr_info("Chip RAM free: %lu bytes\n", mem_avail_chip());
    pr_info("Fast RAM free: %lu bytes\n", mem_avail_fast());

    /* TODO: set up exception handlers */
    /* TODO: initialize display */
    /* TODO: everything else */

    pr_info("Entering idle loop\n");

    for (;;) {
        /* halt until interrupt (if interrupts were enabled) */
    }
}
