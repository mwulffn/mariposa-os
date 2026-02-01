/*
 * mem.c - Bump allocator
 *
 * Phase 1 allocator: simple, no free.
 * Separate heaps for chip and fast RAM.
 */

#include "mem.h"

/* Heap state */
struct heap {
    unsigned long ptr;    /* Next free address */
    unsigned long end;    /* End of heap */
    unsigned long total;  /* Total size (for stats) */
};

static struct heap chip_heap;
static struct heap fast_heap;

/* Align value up to boundary */
static unsigned long align_up(unsigned long val, unsigned long align)
{
    return (val + align - 1) & ~(align - 1);
}

void mem_init(MemEntry *map, void *kernel_end)
{
    chip_heap.ptr = chip_heap.end = chip_heap.total = 0;
    fast_heap.ptr = fast_heap.end = fast_heap.total = 0;

    for (; map->type != MEM_END; map++) {
        if (map->type == MEM_CHIP) {
            chip_heap.ptr = map->base;
            chip_heap.end = map->base + map->size;
            chip_heap.total = map->size;
        } 
        else if (map->type == MEM_FAST) {
            /* Fast heap starts after kernel image */
            unsigned long kend = align_up((unsigned long)kernel_end, 4);
            if (kend > map->base && kend < map->base + map->size) {
                fast_heap.ptr = kend;
            } else {
                fast_heap.ptr = map->base;
            }
            fast_heap.end = map->base + map->size;
            fast_heap.total = fast_heap.end - fast_heap.ptr;
        }
    }
}

static void *heap_alloc(struct heap *h, unsigned long size)
{
    unsigned long align;
    unsigned long ptr;

    if (size == 0)
        return (void *)0;

    /* Alignment: 8 bytes for large allocs, 4 for small */
    align = (size >= 8) ? 8 : 4;
    ptr = align_up(h->ptr, align);

    if (ptr + size > h->end)
        return (void *)0;

    h->ptr = ptr + size;
    return (void *)ptr;
}

void *mem_alloc(unsigned long size, unsigned int flags)
{
    void *p;

    if (flags & ALLOC_CHIP) {
        return heap_alloc(&chip_heap, size);
    }
    
    if (flags & ALLOC_FAST) {
        return heap_alloc(&fast_heap, size);
    }

    /* ALLOC_ANY: try fast first, fall back to chip */
    p = heap_alloc(&fast_heap, size);
    if (p)
        return p;
    
    return heap_alloc(&chip_heap, size);
}

unsigned long mem_avail_chip(void)
{
    return chip_heap.end - chip_heap.ptr;
}

unsigned long mem_avail_fast(void)
{
    return fast_heap.end - fast_heap.ptr;
}
