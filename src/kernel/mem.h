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
 * kernel_end marks end of kernel image in fast RAM, .bss included - which
 * the ROM's reservation of the image does not cover, since it only knows
 * the size of the file it loaded.
 */
void mem_init(struct mem_entry *map, void *kernel_end);

/*
 * Allocate memory.
 * flags: ALLOC_CHIP, ALLOC_FAST, ALLOC_SLOW or ALLOC_ANY
 * Returns NULL on failure, and for a size of zero.
 * Every allocation is 8-byte aligned. Safe in any context.
 */
void *mem_alloc(unsigned long size, unsigned int flags);

/*
 * The same, recording who it is for. owner is opaque - it will be a task
 * pointer - and 0 means the kernel itself. mem_alloc() is owner 0.
 */
void *mem_alloc_tagged(unsigned long size, unsigned int flags, void *owner);

/*
 * Free a block from any pool. NULL is a no-op and returns 0.
 *
 * Returns -1, having changed nothing, for a pointer this allocator did not
 * hand out or has already had back: outside every pool, misaligned, into the
 * middle of a block, freed twice, or with its header trampled by an overrun
 * of the block before it. All of those are bugs in the caller; the point is
 * that they stay the caller's bug instead of becoming a corrupt free list
 * that kills something unrelated ten minutes later.
 */
int mem_free(void *ptr);

/*
 * Free everything owned by `owner`, in every pool. Returns how many blocks
 * that was. For task exit. Owner 0 is refused: the kernel does not exit.
 */
unsigned long mem_free_owner(void *owner);

/*
 * Bytes free in a pool, and the largest single allocation that would
 * succeed right now. pool is ALLOC_CHIP, ALLOC_FAST or ALLOC_SLOW. The
 * second is the one that says whether a screen can be opened.
 */
unsigned long mem_avail(unsigned int pool);
unsigned long mem_largest(unsigned int pool);

/*
 * Walk every pool and verify the structure: headers, physical links, the
 * free list, the accounting. Returns 0 if sound, otherwise a MEMCHK_* code
 * for the first thing found wrong. Costs a full walk with interrupts masked,
 * so it is for tests, debugging and the moments after something odd.
 */
unsigned long mem_check(void);

#define MEMCHK_OK          0
#define MEMCHK_HEADER      1   /* bad magic, size or alignment */
#define MEMCHK_PREV_LINK   2   /* block's prev pointer disagrees with the walk */
#define MEMCHK_UNMERGED    3   /* two free blocks side by side */
#define MEMCHK_OVERRUN     4   /* walk ran past the end of its region */
#define MEMCHK_FREE_LIST   5   /* free list and the walk disagree */
#define MEMCHK_ACCOUNTING  6   /* free byte count is wrong */

/*
 * Convenience wrappers. kmalloc/kfree and chip_alloc/chip_free are the
 * names docs/mem_design.md uses.
 */
#define alloc_chip(size)  mem_alloc((size), ALLOC_CHIP)
#define alloc_fast(size)  mem_alloc((size), ALLOC_FAST)
#define alloc_slow(size)  mem_alloc((size), ALLOC_SLOW)
#define alloc_any(size)   mem_alloc((size), ALLOC_ANY)

#define kmalloc(size)     mem_alloc((size), ALLOC_ANY)
#define kfree(ptr)        mem_free(ptr)
#define chip_alloc(size)  mem_alloc((size), ALLOC_CHIP)
#define chip_free(ptr)    mem_free(ptr)

#define mem_avail_chip()  mem_avail(ALLOC_CHIP)
#define mem_avail_fast()  mem_avail(ALLOC_FAST)
#define mem_avail_slow()  mem_avail(ALLOC_SLOW)
#define chip_largest_free() mem_largest(ALLOC_CHIP)

#endif /* MEM_H */
