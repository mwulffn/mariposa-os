/*
 * memmap.c - memory map surgery
 */
#include "memmap.h"

static int entry_count(const struct mem_entry *map)
{
    int n = 0;

    while (map[n].type != MEM_TYPE_END && n < MEMMAP_MAX_ENTRIES)
        n++;
    return n;
}

/* Make room at `at` by shifting the tail down one slot, terminator included. */
static int insert_slot(struct mem_entry *map, int at)
{
    int n = entry_count(map);
    int i;

    if (n + 2 > MEMMAP_MAX_ENTRIES)      /* +1 new entry, +1 terminator */
        return -1;

    for (i = n; i >= at; i--)
        map[i + 1] = map[i];
    return 0;
}

int memmap_reserve(struct mem_entry *map, unsigned long base,
                   unsigned long size)
{
    int i;

    if (size == 0)
        return 0;

    for (i = 0; map[i].type != MEM_TYPE_END; i++) {
        unsigned long e_base = map[i].base;
        unsigned long e_end  = e_base + map[i].size;
        unsigned long r_end  = base + size;

        if (base < e_base || r_end > e_end)
            continue;                     /* not this one */
        if (map[i].type == MEM_TYPE_RESERVED)
            return 0;                     /* already spoken for */

        /*
         * Three shapes, in order of how much surgery they need. The common
         * one here is the first: the kernel loads at the very start of fast
         * RAM, so the reservation is a prefix.
         */
        if (base == e_base && r_end == e_end) {
            map[i].type = MEM_TYPE_RESERVED;
            return 0;
        }

        if (base == e_base) {             /* prefix: shrink from the front */
            if (insert_slot(map, i) != 0)
                return -1;
            map[i].size  = size;
            map[i].type  = MEM_TYPE_RESERVED;
            map[i + 1].base = r_end;
            map[i + 1].size = e_end - r_end;
            return 0;
        }

        if (r_end == e_end) {             /* suffix: shrink from the back */
            if (insert_slot(map, i + 1) != 0)
                return -1;
            map[i].size     = base - e_base;
            map[i + 1].base = base;
            map[i + 1].size = size;
            map[i + 1].type = MEM_TYPE_RESERVED;
            return 0;
        }

        /* Middle: one free region becomes free, reserved, free. */
        if (insert_slot(map, i + 1) != 0)
            return -1;
        if (insert_slot(map, i + 2) != 0)
            return -1;
        map[i].size     = base - e_base;
        map[i + 1].base = base;
        map[i + 1].size = size;
        map[i + 1].type = MEM_TYPE_RESERVED;
        map[i + 2].base = r_end;
        map[i + 2].size = e_end - r_end;
        return 0;
    }

    return -1;
}
