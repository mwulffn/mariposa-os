/*
 * mem.h - Memory allocator
 */

#ifndef MEM_H
#define MEM_H

/* Memory types from ROM */
#define MEM_END      0
#define MEM_CHIP     1
#define MEM_FAST     2
#define MEM_ROM      5
#define MEM_RESERVED 6

/* Memory flags from ROM */
#define MEMF_TESTED  (1<<0)
#define MEMF_DMA     (1<<1)

/* ROM memory map entry */
typedef struct {
    unsigned long base;
    unsigned long size;
    unsigned short type;
    unsigned short flags;
} MemEntry;

/* Allocation flags */
#define ALLOC_CHIP   (1<<0)   /* Must be chip RAM (DMA capable) */
#define ALLOC_FAST   (1<<1)   /* Must be fast RAM */
#define ALLOC_ANY    0        /* Fast preferred, chip fallback */

/*
 * Initialize memory system from ROM memory map.
 * kernel_end marks end of kernel image in fast RAM.
 */
void mem_init(MemEntry *map, void *kernel_end);

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
#define alloc_any(size)   mem_alloc((size), ALLOC_ANY)

/*
 * Query available memory (for diagnostics)
 */
unsigned long mem_avail_chip(void);
unsigned long mem_avail_fast(void);

#endif /* MEM_H */
