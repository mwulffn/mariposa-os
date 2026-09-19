/*
 * memory.c - memory detection and the map handed to the kernel
 *
 * Amiga-specific and boot-time only, so this stays in the ROM rather than
 * src/shared - the kernel reads the table, it does not build one. What is
 * shared is the table's shape, in src/shared/memmap.h.
 */
#include "memmap.h"
#include "rom.h"

#define CHIP_BASE       0x000000UL
#define KERNEL_CHIP     0x004000UL      /* chip RAM the ROM keeps below here */
#define FAST_BASE       0x200000UL      /* Zorro II */
#define FAST_MAX        0xA00000UL      /* stop sizing here whatever happens */
#define ROM_BASE        0xFC0000UL
#define ROM_SIZE        0x040000UL
#define KERNEL_STACK    0x002000UL      /* 8KB at the top of fast RAM */

#define MEMMAP_TABLE    ((struct mem_entry *)0x3250UL)

#define PATTERN_A       0xAA55AA55UL
#define PATTERN_B       0x55AA55AAUL

#define CUSTOM_COLOR00  ((volatile unsigned short *)0xDFF180UL)

/*
 * Is `probe` real memory rather than a mirror of `base`? Writes a different
 * pattern to each and checks the first survived; 512KB of chip RAM mirrors
 * up the address space, so a single-address test would find memory that is
 * not there. Both locations are saved and restored.
 */
static int distinct_memory(volatile unsigned long *base,
                           volatile unsigned long *probe)
{
    unsigned long save_base  = *base;
    unsigned long save_probe = *probe;
    int distinct;

    *base  = PATTERN_A;
    *probe = PATTERN_B;
    distinct = (*base == PATTERN_A);

    *base  = save_base;
    *probe = save_probe;
    return distinct;
}

unsigned long rom_detect_chip_ram(void)
{
    if (distinct_memory((volatile unsigned long *)0x0FFFF0UL,
                        (volatile unsigned long *)0x1FFFF0UL))
        return 0x200000UL;              /* 2MB, the ECS maximum */

    if (distinct_memory((volatile unsigned long *)0x07FFF0UL,
                        (volatile unsigned long *)0x0FFFF0UL))
        return 0x100000UL;              /* 1MB */

    return 0x080000UL;                  /* 512KB, the Amiga minimum */
}

/* Does this address hold writable memory? Saves and restores. */
static int memory_responds(volatile unsigned long *at)
{
    unsigned long save = *at;
    int ok;

    *at = PATTERN_A;
    ok = (*at == PATTERN_A);
    if (ok)
        *at = save;
    return ok;
}

/*
 * Sizes in one-megabyte steps by probing past the end and letting the
 * read-back fail. That works because an unpopulated Zorro II bus floats
 * rather than aliasing - configure_zorro_ii must have run first.
 */
unsigned long rom_detect_fast_ram(void)
{
    unsigned long size;

    if (!memory_responds((volatile unsigned long *)FAST_BASE))
        return 0;

    for (size = 0x100000UL; size < FAST_MAX; size += 0x100000UL) {
        if (!memory_responds((volatile unsigned long *)(FAST_BASE + size)))
            break;
    }
    return size;
}

/*
 * Walk chip RAM in 4KB steps with two complementary patterns. A failure is
 * not recoverable and not reportable through any normal path, so it turns
 * the screen yellow, says so on the serial port, and stops.
 */
static void test_chip_ram(unsigned long limit)
{
    unsigned long addr;

    for (addr = KERNEL_CHIP; addr < limit; addr += 0x1000UL) {
        volatile unsigned long *at = (volatile unsigned long *)addr;
        unsigned long save = *at;

        *at = PATTERN_A;
        if (*at != PATTERN_A)
            break;
        *at = ~PATTERN_A;
        if (*at != ~PATTERN_A)
            break;
        *at = save;
    }

    if (addr < limit) {
        *CUSTOM_COLOR00 = 0x0FF0;                   /* yellow */
        rom_serial_put_string("CHIP RAM TEST FAILED\n\r");
        for (;;)
            ;
    }
}

static struct mem_entry *add(struct mem_entry *e, unsigned long base,
                             unsigned long size, unsigned short type,
                             unsigned short flags)
{
    e->base  = base;
    e->size  = size;
    e->type  = type;
    e->flags = flags;
    return e + 1;
}

void rom_build_memory_table(void)
{
    struct mem_entry *e = MEMMAP_TABLE;
    unsigned long chip, fast;

    e = add(e, CHIP_BASE, KERNEL_CHIP, MEM_TYPE_RESERVED, MEMF_DMA);

    chip = rom_detect_chip_ram();
    e = add(e, KERNEL_CHIP, chip - KERNEL_CHIP, MEM_TYPE_CHIP, MEMF_DMA);

    test_chip_ram(chip);

    fast = rom_detect_fast_ram();
    if (fast != 0) {
        e = add(e, FAST_BASE, fast - KERNEL_STACK, MEM_TYPE_FAST, MEMF_DMA);
        e = add(e, FAST_BASE + fast - KERNEL_STACK, KERNEL_STACK,
                MEM_TYPE_RESERVED, MEMF_DMA);
    }

    e = add(e, ROM_BASE, ROM_SIZE, MEM_TYPE_ROM, 0);
    add(e, 0, 0, MEM_TYPE_END, 0);
}

/*
 * Mark the loaded kernel's image reserved.
 *
 * The table is built long before SYSTEM.BIN is read, so until this runs the
 * fast RAM entry starts at $200000 and hands the kernel's own image out as
 * free memory. mem.c has been clamping to its _end symbol to avoid
 * allocating over itself; this makes the table tell the truth instead.
 *
 * Rounded up to 4KB so the kernel's heap starts on a page-ish boundary.
 */
unsigned long rom_reserve_kernel_image(unsigned long bytes)
{
    unsigned long size = (bytes + 0xFFFUL) & ~0xFFFUL;

    if (memmap_reserve(MEMMAP_TABLE, FAST_BASE, size) != 0)
        return (unsigned long)-1;
    return 0;
}

/* ------------------------------------------------------------- printing --- */

static const char *type_name(unsigned short type)
{
    if (type == MEM_TYPE_RESERVED) return "Reserved";
    if (type == MEM_TYPE_CHIP)     return "Chip";
    if (type == MEM_TYPE_FAST)     return "Fast";
    if (type == MEM_TYPE_ROM)      return "ROM";
    return "???";
}

void rom_print_memory_map(void)
{
    const struct mem_entry *e = MEMMAP_TABLE;

    rom_serial_put_string("Memory Map:\n\r");

    for (; e->type != MEM_TYPE_END; e++) {
        unsigned long args[4];

        if (e->base == 0 && e->size == 0)
            break;

        args[0] = e->base;
        args[1] = e->base + e->size - 1;
        args[2] = (unsigned long)type_name(e->type);
        args[3] = e->size >> 10;                 /* KB */
        rom_printf("  $%.lx-$%.lx: %s ($%.lx KB)", args);

        if (e->flags & MEMF_DMA)
            rom_serial_put_string(" [DMA]");
        rom_serial_put_string("\n\r");
    }
}
