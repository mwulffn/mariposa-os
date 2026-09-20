/*
 * mem.c - free-list allocator with coalescing
 *
 * Three pools - chip, fast, slow - sharing one mechanism, per
 * docs/mem_design.md. Chip is best-fit, because what matters there is
 * keeping a hole big enough for the next bitmap; fast and slow are
 * first-fit, because what matters there is being quick.
 *
 * A pool is one or more regions from the ROM's memory map. Each region is a
 * run of blocks laid end to end, closed by a zero-length used block so that
 * "the block after this one" always exists and coalescing never has to ask
 * whether it is at the end:
 *
 *     | hdr payload... | hdr payload... | hdr payload... | end hdr |
 *
 * Every block carries a 16-byte header. A free block also uses the first 8
 * bytes of its payload as doubly-linked free list pointers, which is why
 * the smallest payload is 8 bytes and why the list costs a used block
 * nothing. Doubly linked, where the design doc says singly, because
 * coalescing unlinks arbitrary blocks and with one link that is a walk of
 * the whole list for every free.
 *
 * The header's magic is what makes mem_free able to refuse a bad pointer,
 * and it doubles as an overrun canary: it sits directly after the previous
 * block's payload, so writing off the end of an allocation tramples it and
 * mem_check says so.
 *
 * Every entry point is a critical section. The allocator will be called
 * from tasks and from ISRs, and a free list is exactly the kind of state
 * that must not be seen half updated.
 */

#include "mem.h"
#include "cpu.h"

#define ALIGN        8UL
#define HDR_SIZE     16UL
#define MIN_BLOCK    (HDR_SIZE + 8UL)   /* header plus the two list links */

#define MAGIC_USED   0x414C4F43UL       /* 'ALOC' */
#define MAGIC_FREE   0x46524545UL       /* 'FREE' */

struct block {
    unsigned long  size;        /* whole block, header included; 8-aligned */
    struct block  *prev;        /* physically previous block, NULL if first */
    void          *owner;       /* who it is for; 0 is the kernel */
    unsigned long  magic;       /* MAGIC_USED or MAGIC_FREE */
};

/* Lives in a free block's payload. */
struct links {
    struct block *next;
    struct block *prev;
};

#define PAYLOAD(b)   ((void *)((char *)(b) + HDR_SIZE))
#define LINKS(b)     ((struct links *)PAYLOAD(b))
#define NEXT_PHYS(b) ((struct block *)((char *)(b) + (b)->size))

#define MAX_REGIONS  8

struct region {
    struct block *first;
    struct block *end;          /* the closing marker */
};

struct pool {
    struct block *free_list;    /* most recently freed first */
    unsigned long free_bytes;   /* payload bytes across the free list */
    int           best_fit;
    int           nregions;
    struct region regions[MAX_REGIONS];
};

enum { POOL_CHIP, POOL_FAST, POOL_SLOW, NPOOLS };

static struct pool pools[NPOOLS];

/* ALLOC_ANY: fast, then slow, then chip. See mem.h for why that order. */
static const int any_order[NPOOLS] = { POOL_FAST, POOL_SLOW, POOL_CHIP };

/* ------------------------------------------------------------ free list --- */

static void list_push(struct pool *p, struct block *b)
{
    b->magic = MAGIC_FREE;
    b->owner = 0;
    LINKS(b)->prev = 0;
    LINKS(b)->next = p->free_list;
    if (p->free_list)
        LINKS(p->free_list)->prev = b;
    p->free_list = b;
    p->free_bytes += b->size - HDR_SIZE;
}

static void list_remove(struct pool *p, struct block *b)
{
    struct links *l = LINKS(b);

    if (l->prev)
        LINKS(l->prev)->next = l->next;
    else
        p->free_list = l->next;
    if (l->next)
        LINKS(l->next)->prev = l->prev;
    p->free_bytes -= b->size - HDR_SIZE;
}

/* ----------------------------------------------------------------- init --- */

static void add_region(struct pool *p, unsigned long base, unsigned long end)
{
    struct block *b, *e;

    base = (base + ALIGN - 1) & ~(ALIGN - 1);
    end &= ~(ALIGN - 1);
    if (p->nregions == MAX_REGIONS || end < base ||
        end - base < MIN_BLOCK + HDR_SIZE)
        return;

    b = (struct block *)base;
    e = (struct block *)(end - HDR_SIZE);

    b->size = (unsigned long)e - base;
    b->prev = 0;

    e->size  = 0;               /* never merged into, never walked past */
    e->prev  = b;
    e->owner = 0;
    e->magic = MAGIC_USED;

    p->regions[p->nregions].first = b;
    p->regions[p->nregions].end   = e;
    p->nregions++;
    list_push(p, b);
}

void mem_init(struct mem_entry *map, void *kernel_end)
{
    unsigned long kend = (unsigned long)kernel_end;
    int i;

    for (i = 0; i < NPOOLS; i++) {
        pools[i].free_list  = 0;
        pools[i].free_bytes = 0;
        pools[i].nregions   = 0;
        pools[i].best_fit   = (i == POOL_CHIP);
    }

    for (; map->type != MEM_TYPE_END; map++) {
        unsigned long base = map->base;
        unsigned long end  = map->base + map->size;
        struct pool *p;

        if (map->type == MEM_TYPE_CHIP)      p = &pools[POOL_CHIP];
        else if (map->type == MEM_TYPE_FAST) p = &pools[POOL_FAST];
        else if (map->type == MEM_TYPE_SLOW) p = &pools[POOL_SLOW];
        else continue;

        /*
         * The ROM carves the kernel out of the map as MEM_TYPE_RESERVED,
         * but it reserves the file it loaded, rounded up to 4KB, and a
         * file has no .bss in it. Whenever the image plus .bss crosses
         * that round-up, the tail of the kernel's own variables lies in a
         * region the map calls free. kernel_end is the truth.
         */
        if (kend > base && kend < end)
            base = kend;

        add_region(p, base, end);
    }
}

/* ---------------------------------------------------------------- alloc --- */

static struct block *find_fit(struct pool *p, unsigned long need)
{
    struct block *b, *best = 0;

    for (b = p->free_list; b; b = LINKS(b)->next) {
        if (b->size < need)
            continue;
        if (!p->best_fit || b->size == need)
            return b;
        if (!best || b->size < best->size)
            best = b;
    }
    return best;
}

static void *pool_alloc(struct pool *p, unsigned long need, void *owner)
{
    struct block *b = find_fit(p, need);

    if (!b)
        return 0;
    list_remove(p, b);

    /* Split off the tail if what is left over can stand as a block. */
    if (b->size - need >= MIN_BLOCK) {
        struct block *rest = (struct block *)((char *)b + need);

        rest->size = b->size - need;
        rest->prev = b;
        NEXT_PHYS(rest)->prev = rest;
        b->size = need;
        list_push(p, rest);
    }

    b->magic = MAGIC_USED;
    b->owner = owner;
    return PAYLOAD(b);
}

void *mem_alloc_tagged(unsigned long size, unsigned int flags, void *owner)
{
    unsigned long need;
    void *ptr = 0;
    int i;

    /* Bounding size first keeps the round-up below from wrapping to a small
     * number and succeeding. No pool is anywhere near 2GB on a 24-bit bus. */
    if (size == 0 || size > 0x7FFFFFFFUL)
        return 0;
    need = ((size + ALIGN - 1) & ~(ALIGN - 1)) + HDR_SIZE;
    if (need < MIN_BLOCK)
        need = MIN_BLOCK;

    CRITICAL_ENTER();
    if (flags & ALLOC_CHIP)
        ptr = pool_alloc(&pools[POOL_CHIP], need, owner);
    else if (flags & ALLOC_FAST)
        ptr = pool_alloc(&pools[POOL_FAST], need, owner);
    else if (flags & ALLOC_SLOW)
        ptr = pool_alloc(&pools[POOL_SLOW], need, owner);
    else
        for (i = 0; i < NPOOLS && !ptr; i++)
            ptr = pool_alloc(&pools[any_order[i]], need, owner);
    CRITICAL_EXIT();
    return ptr;
}

void *mem_alloc(unsigned long size, unsigned int flags)
{
    return mem_alloc_tagged(size, flags, 0);
}

/* ----------------------------------------------------------------- free --- */

/* The pool whose memory contains a block header at b, or NULL. Checked
 * before b is dereferenced, so a wild pointer is never read through - on
 * this machine a stray read can be a hardware register with side effects. */
static struct pool *pool_of(const struct block *b)
{
    int i, r;

    for (i = 0; i < NPOOLS; i++)
        for (r = 0; r < pools[i].nregions; r++)
            if (b >= pools[i].regions[r].first && b < pools[i].regions[r].end)
                return &pools[i];
    return 0;
}

static void free_block(struct pool *p, struct block *b)
{
    struct block *next = NEXT_PHYS(b);

    if (next->magic == MAGIC_FREE) {
        list_remove(p, next);
        b->size += next->size;
    }
    if (b->prev && b->prev->magic == MAGIC_FREE) {
        list_remove(p, b->prev);
        b->prev->size += b->size;
        b = b->prev;
    }
    NEXT_PHYS(b)->prev = b;
    list_push(p, b);
}

/* Is b a live allocation we can safely unthread? Magic alone is not enough:
 * payload data can spell 'ALOC'. The neighbours have to agree as well. */
static int block_is_sound(const struct pool *p, const struct block *b)
{
    const struct block *next;

    if (b->magic != MAGIC_USED || b->size < MIN_BLOCK || (b->size & (ALIGN - 1)))
        return 0;
    next = NEXT_PHYS(b);
    if (pool_of(next) != p && next->size != 0)
        return 0;
    if (next->prev != b)
        return 0;
    if (next->magic != MAGIC_USED && next->magic != MAGIC_FREE)
        return 0;
    return 1;
}

int mem_free(void *ptr)
{
    struct block *b = (struct block *)((char *)ptr - HDR_SIZE);
    struct pool *p;
    int rc = -1;

    if (!ptr)
        return 0;
    if ((unsigned long)ptr & (ALIGN - 1))
        return -1;

    CRITICAL_ENTER();
    p = pool_of(b);
    if (p && block_is_sound(p, b)) {
        free_block(p, b);
        rc = 0;
    }
    CRITICAL_EXIT();
    return rc;
}

unsigned long mem_free_owner(void *owner)
{
    unsigned long count = 0;
    int i, r;

    if (!owner)
        return 0;

    CRITICAL_ENTER();
    for (i = 0; i < NPOOLS; i++) {
        struct pool *p = &pools[i];

        for (r = 0; r < p->nregions; r++) {
            struct block *b = p->regions[r].first;

            while (b != p->regions[r].end) {
                if (b->magic == MAGIC_USED && b->owner == owner) {
                    free_block(p, b);
                    count++;
                    /* b may have been absorbed by the block before it. The
                     * walk restarts from whatever now covers this address. */
                    if (b->prev && b->prev->magic == MAGIC_FREE &&
                        NEXT_PHYS(b->prev) != b)
                        b = b->prev;
                }
                b = NEXT_PHYS(b);
            }
        }
    }
    CRITICAL_EXIT();
    return count;
}

/* -------------------------------------------------------------- queries --- */

static struct pool *pool_for(unsigned int flag)
{
    if (flag & ALLOC_CHIP) return &pools[POOL_CHIP];
    if (flag & ALLOC_FAST) return &pools[POOL_FAST];
    if (flag & ALLOC_SLOW) return &pools[POOL_SLOW];
    return 0;
}

unsigned long mem_avail(unsigned int pool)
{
    struct pool *p = pool_for(pool);
    unsigned long n;

    if (!p)
        return 0;
    CRITICAL_ENTER();
    n = p->free_bytes;
    CRITICAL_EXIT();
    return n;
}

unsigned long mem_largest(unsigned int pool)
{
    struct pool *p = pool_for(pool);
    struct block *b;
    unsigned long best = 0;

    if (!p)
        return 0;
    CRITICAL_ENTER();
    for (b = p->free_list; b; b = LINKS(b)->next)
        if (b->size - HDR_SIZE > best)
            best = b->size - HDR_SIZE;
    CRITICAL_EXIT();
    return best;
}

/* ---------------------------------------------------------------- check --- */

static unsigned long check_pool(struct pool *p)
{
    unsigned long walk_free = 0, walk_bytes = 0, list_free = 0;
    struct block *b, *prev;
    int r;

    for (r = 0; r < p->nregions; r++) {
        prev = 0;
        for (b = p->regions[r].first; b != p->regions[r].end; b = NEXT_PHYS(b)) {
            if (b > p->regions[r].end)
                return MEMCHK_OVERRUN;
            if ((b->magic != MAGIC_USED && b->magic != MAGIC_FREE) ||
                b->size < MIN_BLOCK || (b->size & (ALIGN - 1)))
                return MEMCHK_HEADER;
            if (b->prev != prev)
                return MEMCHK_PREV_LINK;
            if (b->magic == MAGIC_FREE) {
                if (prev && prev->magic == MAGIC_FREE)
                    return MEMCHK_UNMERGED;
                walk_free++;
                walk_bytes += b->size - HDR_SIZE;
            }
            prev = b;
        }
        if (b->prev != prev || b->size != 0 || b->magic != MAGIC_USED)
            return MEMCHK_PREV_LINK;
    }

    prev = 0;
    for (b = p->free_list; b; b = LINKS(b)->next) {
        if (pool_of(b) != p || b->magic != MAGIC_FREE || LINKS(b)->prev != prev)
            return MEMCHK_FREE_LIST;
        if (++list_free > walk_free)
            return MEMCHK_FREE_LIST;        /* also ends a looped list */
        prev = b;
    }
    if (list_free != walk_free)
        return MEMCHK_FREE_LIST;
    if (walk_bytes != p->free_bytes)
        return MEMCHK_ACCOUNTING;
    return MEMCHK_OK;
}

unsigned long mem_check(void)
{
    unsigned long rc = MEMCHK_OK;
    int i;

    CRITICAL_ENTER();
    for (i = 0; i < NPOOLS && rc == MEMCHK_OK; i++)
        rc = check_pool(&pools[i]);
    CRITICAL_EXIT();
    return rc;
}
