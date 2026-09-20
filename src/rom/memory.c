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

/*
 * Slow RAM: the trapdoor expansion at $C00000. 512KB on a stock A501, up to
 * 1.5MB on the clones that fill Gary's whole decode. SLOW_MAX stops the
 * probe at 1.5MB, so the highest address it ever touches is $D00000.
 *
 * That ceiling is not a guess. $D80000 up is Gayle, then the battery clock
 * at $DC0000 and the custom chips at $DFF000, and this probe writes before
 * it reads. Sizing one step further would put a longword into hardware
 * registers, which is a good way to wedge the machine on a real A600.
 */
#define SLOW_BASE       0xC00000UL
#define SLOW_STEP       0x080000UL      /* 512KB, the trapdoor unit */
#define SLOW_MAX        0x180000UL      /* 1.5MB, Gary's whole decode */
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
 * Is there real slow RAM `offset` bytes above SLOW_BASE?
 *
 * Both halves of this are needed, and each catches what the other misses.
 * Measured on FS-UAE with and without a 512KB board fitted:
 *
 *   - Unpopulated, the region floats. It does not float high the way the
 *     Zorro II bus does, so memory_responds is the test that settles it:
 *     the read-back was $24822482 against a written $AA55AA55, and the idle
 *     value changed between two consecutive reads. Anything that only read
 *     would be reading whatever the chipset last drove onto the bus.
 *
 *   - Populated but partially decoded, the board mirrors itself up the
 *     region, and memory_responds would happily "find" the same 512KB three
 *     times over. distinct_memory is the same two-pattern test chip RAM
 *     sizing uses, for the same reason.
 */
static int slow_ram_present(unsigned long offset)
{
    volatile unsigned long *base  = (volatile unsigned long *)SLOW_BASE;
    volatile unsigned long *probe =
        (volatile unsigned long *)(SLOW_BASE + offset);

    if (!memory_responds(probe))
        return 0;
    return distinct_memory(base, probe);
}

unsigned long rom_detect_slow_ram(void)
{
    unsigned long size;

    if (!memory_responds((volatile unsigned long *)SLOW_BASE))
        return 0;

    for (size = SLOW_STEP; size < SLOW_MAX; size += SLOW_STEP) {
        if (!slow_ram_present(size))
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

/*
 * MEMF_DMA means the chipset can reach it, so it belongs to chip RAM and to
 * nothing else. Zorro II fast RAM sits outside the chip bus entirely: the
 * blitter, Paula and the copper cannot touch it, and a buffer allocated
 * there for DMA would quietly transfer garbage. Fast therefore carries no
 * flags at all - it is not DMA-capable, and detect_fast_ram only probes one
 * long per megabyte rather than testing it.
 *
 * MEMF_TESTED goes on the one region test_chip_ram actually walks.
 */
void rom_build_memory_table(void)
{
    struct mem_entry *e = MEMMAP_TABLE;
    unsigned long chip, fast, slow;

    e = add(e, CHIP_BASE, KERNEL_CHIP, MEM_TYPE_RESERVED, MEMF_DMA);

    chip = rom_detect_chip_ram();
    e = add(e, KERNEL_CHIP, chip - KERNEL_CHIP, MEM_TYPE_CHIP,
            MEMF_DMA | MEMF_TESTED);

    test_chip_ram(chip);

    fast = rom_detect_fast_ram();
    if (fast != 0) {
        e = add(e, FAST_BASE, fast - KERNEL_STACK, MEM_TYPE_FAST, 0);
        e = add(e, FAST_BASE + fast - KERNEL_STACK, KERNEL_STACK,
                MEM_TYPE_RESERVED, 0);
    }

    /*
     * Slow RAM last of the RAM entries, so the table stays in address
     * order. No flags: it is on the chip bus but Agnus cannot DMA to it,
     * and the probe only writes one longword per 512KB rather than walking
     * it, so it is no more MEMF_TESTED than Zorro fast RAM is.
     */
    slow = rom_detect_slow_ram();
    if (slow != 0)
        e = add(e, SLOW_BASE, slow, MEM_TYPE_SLOW, 0);

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

/*
 * Top of the kernel's stack: the highest reserved region in fast RAM.
 *
 * bootstrap.s used to walk the table itself and take the first RESERVED
 * entry at or above $200000. That was right only while the stack was the
 * only such entry. reserve_kernel_image then started carving the loaded
 * image out as RESERVED at $200000, which comes first, so the kernel was
 * handed $201000 - the top of its own image - as a stack and spent every
 * boot writing its call frames over its own code. It survived only because
 * the image happened to end a few hundred bytes short of the round-up.
 *
 * Picking the highest instead of the first is what the search always meant:
 * build_memory_table puts the stack at the very top of fast RAM, so nothing
 * reserved can legitimately sit above it.
 *
 * Returns 0 if there is no reserved region in fast RAM at all.
 */
unsigned long rom_kernel_stack_top(void)
{
    const struct mem_entry *e = MEMMAP_TABLE;
    unsigned long top = 0;
    unsigned long best = 0;

    for (; e->type != MEM_TYPE_END; e++) {
        if (e->type != MEM_TYPE_RESERVED)
            continue;
        if (e->base < FAST_BASE || e->base >= FAST_MAX)
            continue;                    /* chip below, slow RAM above */
        if (e->base >= best) {
            best = e->base;
            top  = e->base + e->size;
        }
    }
    return top;
}

/* ------------------------------------------------------------- printing --- */

static const char *type_name(unsigned short type)
{
    if (type == MEM_TYPE_RESERVED) return "Reserved";
    if (type == MEM_TYPE_CHIP)     return "Chip";
    if (type == MEM_TYPE_FAST)     return "Fast";
    if (type == MEM_TYPE_SLOW)     return "Slow";
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
        if (e->flags & MEMF_TESTED)
            rom_serial_put_string(" [TESTED]");
        rom_serial_put_string("\n\r");
    }
}
