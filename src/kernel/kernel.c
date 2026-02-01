/*
 * kernel.c - Minimal kernel
 */

#include "amiga_hw.h"
#include "mem.h"
#include "serial.h"
#include "kprintf.h"

/* End of kernel image (from linker) */
extern char _end[];

/* ROM panic function - set by crt0 */
extern void (*rom_panic)(void);

void kernel_main(MemEntry *memmap)
{
    /* Purple background - proof that C code is running */
    custom.color[0] = RGB4(8,0,8);

    /* Initialize serial (ROM did this, but ensure known state) */
    ser_init();

    pr_info("\n");
    pr_info("=====================================\n");
    pr_info("Kernel starting\n");
    pr_info("=====================================\n");

    /* Initialize memory allocator */
    mem_init(memmap, _end);

    pr_info("Memory initialized:\n");
    pr_info("  Chip RAM free: %lu bytes\n", mem_avail_chip());
    pr_info("  Fast RAM free: %lu bytes\n", mem_avail_fast());
    pr_info("  Kernel ends at: %p\n", _end);

    /* TODO: set up exception handlers */
    /* TODO: initialize display */
    /* TODO: everything else */

    pr_info("Entering idle loop\n");

    for (;;) {
        /* halt until interrupt (if interrupts were enabled) */
    }
}
