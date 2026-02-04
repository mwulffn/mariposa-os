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

static const char *mem_type_name(unsigned short type)
{
    switch (type) {
        case MEM_END:      return "END";
        case MEM_CHIP:     return "CHIP";
        case MEM_FAST:     return "FAST";
        case MEM_ROM:      return "ROM";
        case MEM_RESERVED: return "RESERVED";
        default:           return "UNKNOWN";
    }
}

static void print_memory_map(MemEntry *map)
{
    int entry = 0;

    pr_info("\n=== Memory Map ===\n");
    pr_info("Entry  Base       Size       Type      Flags\n");
    pr_info("-----  ---------- ---------- --------- -----\n");

    while (map->type != MEM_END) {
        pr_info("%5d  $%08lx $%08lx %-9s $%04x\n",
                entry,
                map->base,
                map->size,
                mem_type_name(map->type),
                map->flags);
        map++;
        entry++;
    }

    pr_info("==================\n\n");
}

void kernel_main(MemEntry *memmap)
{
    /* Initialize serial */
    ser_init();

    pr_info("\n");
    pr_info("Kernel starting successfully!\n");

    /* Print memory map received from ROM */
    print_memory_map(memmap);

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

