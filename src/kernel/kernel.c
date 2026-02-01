/*
 * kernel.c - Minimal kernel
 */

#include "amiga_hw.h"
#include "mem.h"
#include "serial.h"
#include "kprintf.h"
#include "stdarg.h"

/* Linker symbols (vbcc adds underscore, so _end becomes __end) */
extern char _end;        /* Will become __end in assembly (matches linker script) */
extern char _bss_start;  /* Will become __bss_start in assembly */
extern char _bss_end;    /* Will become __bss_end in assembly */

/* ROM panic function - set by crt0 */
extern void (*rom_panic)(void);

int test(int a, ...)
{
    va_list ap;
    va_start(ap, a);
    int b = va_arg(ap, int);
    int c = va_arg(ap, int);
    va_end(ap);
    return b + c;
}


void kernel_main(MemEntry *memmap)
{
    /* Purple background */
    int res = test(1,2,3);

    if(res==5)
        custom.color[0] = RGB4(10,0,0);
    else
        custom.color[0] = RGB4(8,10,8);


    /* Initialize serial */
    ser_init();

    test_pointers();

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

void test_pointers(void)
{
    pr_info("\nTesting kprintf with pointers:\n");
    pr_info("kernel_main = %p\n", kernel_main);
    pr_info("&kernel_main = %p\n", &kernel_main);

    void *p = (void *)0x200000;
    pr_info("literal ptr = %p\n", p);
    pr_info("Two pointers: %p and %p\n", kernel_main, p);
}
