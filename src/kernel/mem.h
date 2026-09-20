/*
 * mem.h - Memory allocator
 */

#ifndef MEM_H
#define MEM_H

/*
 * The map's shape, its types and its flags come from the ROM, so they are
 * declared once in src/shared/memmap.h and included here. This file used to
 * restate them as MemEntry and its own MEM_* equates, with MEMF_TESTED and
 * MEMF_DMA on the opposite bits from the ones the ROM writes - the exact
 * drift that having two declarations invites.
 */
#include "memmap.h"

/*
 * Allocation flags. Each named flag is a hard requirement - if that pool is
 * empty the allocation fails rather than quietly handing back something
 * with different properties. ALLOC_CHIP means "the chipset must be able to
 * reach this"; ALLOC_FAST means "this must not contend with the chipset".
 * Neither is satisfiable by slow RAM, which is why it has its own flag.
 */
#define ALLOC_CHIP   (1<<0)   /* Must be chip RAM (DMA capable) */
#define ALLOC_FAST   (1<<1)   /* Must be Zorro II fast RAM */
#define ALLOC_SLOW   (1<<2)   /* Must be $C00000 trapdoor RAM */

/*
 * ALLOC_ANY takes whatever is going, in the order that leaves the machine
 * with the most useful memory left: fast first, then slow, then chip.
 *
 * Slow RAM comes second rather than being lumped in with fast because it is
 * on the chip bus and runs at roughly chip speed - see MEM_TYPE_SLOW in
 * memmap.h. Chip RAM is last because it is the only memory the chipset can
 * DMA to, so spending it on anything that did not ask for it is waste.
 */
#define ALLOC_ANY    0

/*
 * Initialize memory system from ROM memory map.
 * kernel_end marks end of kernel image in fast RAM.
 */
void mem_init(struct mem_entry *map, void *kernel_end);

/*
 * Allocate memory.
 * flags: ALLOC_CHIP, ALLOC_FAST, or ALLOC_ANY
 * Returns NULL on failure.
 * Alignment: 4 bytes minimum, 8 for size >= 8
 */
void *mem_alloc(unsigned long size, unsigned int flags);

/*
 * Convenience wrappers
 */
#define alloc_chip(size)  mem_alloc((size), ALLOC_CHIP)
#define alloc_fast(size)  mem_alloc((size), ALLOC_FAST)
#define alloc_slow(size)  mem_alloc((size), ALLOC_SLOW)
#define alloc_any(size)   mem_alloc((size), ALLOC_ANY)

/*
 * Query available memory (for diagnostics)
 */
unsigned long mem_avail_chip(void);
unsigned long mem_avail_fast(void);
unsigned long mem_avail_slow(void);

#endif /* MEM_H */
