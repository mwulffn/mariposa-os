/*
 * kernel.c - Minimal kernel
 */

#include "amiga_hw.h"
#include "bootinfo.h"
#include "mem.h"
#include "serial.h"
#include "kprintf.h"
#include "stdarg.h"
#include "task.h"
#include "console.h"
#include "vector.h"
#include "irq.h"
#include "cpu.h"

/* Linker symbols (vbcc adds underscore, so _end becomes __end) */
extern char _text_start;  /* Start of the kernel image (linker script) */
extern char _end;        /* Will become __end in assembly (matches linker script) */
extern char _bss_start;  /* Will become __bss_start in assembly */
extern char _bss_end;    /* Will become __bss_end in assembly */

/* ROM panic function - set by crt0 */
extern void (*rom_panic)(void);

/* Top of the stack the ROM handed us in A7, captured by crt0. */
extern unsigned long stack_top;

static const char *mem_type_name(unsigned short type)
{
    switch (type) {
        case MEM_TYPE_END:      return "END";
        case MEM_TYPE_CHIP:     return "CHIP";
        case MEM_TYPE_FAST:     return "FAST";
        case MEM_TYPE_SLOW:     return "SLOW";
        case MEM_TYPE_ROM:      return "ROM";
        case MEM_TYPE_RESERVED: return "RESERVED";
        default:           return "UNKNOWN";
    }
}

/*
 * The hex stays alongside the names: these are the only two flags the ROM
 * defines today, so a bit outside the pair would print as no name at all
 * and the raw word is the only thing that would show it.
 */
static const char *mem_flag_names(unsigned short flags)
{
    switch (flags & (MEMF_DMA | MEMF_TESTED)) {
        case MEMF_DMA | MEMF_TESTED: return "DMA TESTED";
        case MEMF_DMA:               return "DMA";
        case MEMF_TESTED:            return "TESTED";
        default:                     return "-";
    }
}

/*
 * Where we are and how much room the stack has.
 *
 * This exists because it was missing. The ROM picked the kernel stack by
 * walking the memory map for the first reserved region above $200000, which
 * silently became the loaded kernel image itself, so the kernel booted with
 * its stack a few hundred bytes above its own code and quietly wrote call
 * frames over itself. Nothing reported the stack, so nothing showed it: the
 * only symptom was that adding code to the kernel made it crash somewhere
 * unrelated. One line at boot makes the next one of these obvious.
 *
 * The size comes from the map rather than from the ROM, which is the point:
 * it is a cross-check of the region the ROM says it reserved against the
 * pointer it actually handed over.
 */
static void print_image_and_stack(struct mem_entry *map)
{
    unsigned long img_start = (unsigned long)&_text_start;
    unsigned long img_end   = (unsigned long)&_end;
    unsigned long stack_base = 0;
    unsigned long stack_size = 0;

    for (; map->type != MEM_TYPE_END; map++) {
        if (map->type != MEM_TYPE_RESERVED)
            continue;
        if (map->base + map->size != stack_top)
            continue;
        stack_base = map->base;
        stack_size = map->size;
        break;
    }

    pr_info("Image: $%08lx-$%08lx (%lu bytes)\n",
            img_start, img_end, img_end - img_start);
    pr_info("Stack: top $%08lx, %lu bytes down to $%08lx\n",
            stack_top, stack_size, stack_base);

    if (stack_size == 0) {
        pr_info("  WARNING: no reserved region in the map ends at the "
                "stack top - the ROM handed over a stack it never "
                "reserved\n");
        return;
    }

    /* The stack grows down from stack_top through stack_base. If that range
     * touches the image at all, we are already overwriting ourselves. */
    if (stack_base < img_end && stack_top > img_start)
        pr_info("  WARNING: stack $%08lx-$%08lx overlaps the kernel image "
                "- it will overwrite its own code\n",
                stack_base, stack_top);
}

static void print_memory_map(struct mem_entry *map)
{
    int entry = 0;

    pr_info("\n=== Memory Map ===\n");
    pr_info("Entry  Base       Size       Type      Flags\n");
    pr_info("-----  ---------- ---------- --------- ----------------\n");

    while (map->type != MEM_TYPE_END) {
        pr_info("%5d  $%08lx $%08lx %-9s $%04x %s\n",
                entry,
                map->base,
                map->size,
                mem_type_name(map->type),
                map->flags,
                mem_flag_names(map->flags));
        map++;
        entry++;
    }

    pr_info("==================\n\n");
}

static void print_boot_device(const struct bootinfo *bi)
{
    /* Absent is not the same as none: a ROM older than the field cannot
     * say, a ROM that found no disk says BOOTDEV_NONE. */
    if (!BOOTINFO_HAS(bi, boot_part_blocks)) {
        pr_info("Boot device: not reported by this ROM\n");
        return;
    }
    if (bi->boot_dev_type == BOOTDEV_IDE)
        pr_info("Boot device: IDE unit %u, partition at block %lu, "
                "%lu blocks\n", (unsigned)bi->boot_dev_unit,
                bi->boot_part_lba, bi->boot_part_blocks);
    else
        pr_info("Boot device: none\n");
}

/* The handoff, kept for whoever needs it later - the block layer will want
 * the boot partition. Valid once kernel_main has checked it. */
const struct bootinfo *bootinfo;

void kernel_main(struct bootinfo *bi)
{
    struct mem_entry *memmap;

    /* Initialize serial */
    ser_init();

    /*
     * Check the handoff before believing any of it. The likely way to get
     * here with a bad one is a ROM from before the struct existed, which
     * passes the memory map in A0 - and a memory map walked as a bootinfo
     * yields plausible-looking pointers. rom_panic came in A1 as well as in
     * the struct precisely so that this can still be reported.
     */
    if (bi->magic != BOOTINFO_MAGIC || bi->version < 1 ||
        !BOOTINFO_HAS(bi, stack_top)) {
        pr_info("\nKERNEL: bad boot info at $%08lx (magic $%08lx) - "
                "ROM and kernel out of step?\n",
                (unsigned long)bi, bi->magic);
        rom_panic();
    }
    bootinfo = bi;
    memmap = bi->memmap;

    pr_info("\n");
    pr_info("Kernel starting successfully!\n");
    pr_info("Boot info v%u, %u bytes, from ROM v%u\n", (unsigned)bi->version,
            (unsigned)bi->size,
            BOOTINFO_HAS(bi, rom_version) ? (unsigned)bi->rom_version : 0u);
    print_boot_device(bi);

    print_image_and_stack(memmap);

    /* Print memory map received from ROM */
    print_memory_map(memmap);

    /* Initialize memory allocator */
    mem_init(memmap, &_end);

    pr_info("Memory system initialized\n");
    pr_info("Fast RAM free: %lu bytes\n", mem_avail_fast());
    pr_info("Slow RAM free: %lu bytes\n", mem_avail_slow());
    pr_info("Chip RAM free: %lu bytes, largest block %lu\n",
            mem_avail_chip(), chip_largest_free());
    if (mem_check() != MEMCHK_OK)
        pr_info("  WARNING: heap check failed (%lu) straight after init\n",
                mem_check());

    /* TODO: set up exception handlers */
    /* TODO: initialize display */
    /* TODO: everything else */

    /* Arm interrupts. Serial switches from polled to its ring buffer here,
     * so everything above was on the wire before it returned and everything
     * below is queued. sched_init comes first: it records which CPU this
     * is, and irq_init installs vectors, which needs to know. */
    sched_init(BOOTINFO_HAS(bi, cpu_type) ? bi->cpu_type : CPU_68000);
    pr_info("CPU: 680%02lu%s\n", cpu_type == CPU_68000 ? 0UL : cpu_type * 10,
            BOOTINFO_HAS(bi, fpu_type) && bi->fpu_type ? " with FPU" : "");
    irq_init();
    cpu_int_enable();

    if (console_init() != 0)
        pr_info("WARNING: no console\n");

    pr_info("Starting scheduler\n");
    sched_start();
}
