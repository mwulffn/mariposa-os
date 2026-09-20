/*
 * memmap.h - the memory map the ROM builds and the kernel reads
 *
 * The one structure that genuinely crosses the handoff: the ROM writes this
 * table at MEMMAP_TABLE and passes its address to the kernel in A0. It was
 * declared twice, as equates in hardware.i and as MemEntry in the kernel's
 * mem.h, which is exactly the kind of duplication that drifts.
 */
#ifndef MEMMAP_H
#define MEMMAP_H

#define MEM_TYPE_END       0
#define MEM_TYPE_CHIP      1
#define MEM_TYPE_FAST      2
#define MEM_TYPE_SLOW      3
#define MEM_TYPE_ROM       5
#define MEM_TYPE_RESERVED  6

/*
 * MEM_TYPE_SLOW is the $C00000 trapdoor expansion - "slow", "ranger" or
 * "bogo" RAM, the A501 and its clones. It is its own type rather than more
 * MEM_TYPE_FAST because it is neither fast nor chip:
 *
 *   - It sits on the chip bus, so the CPU contends with the chipset for it
 *     and it runs at roughly chip RAM speed, not Zorro II speed.
 *   - Agnus cannot address it for DMA, so it is not MEMF_DMA either.
 *
 * An allocator that saw it as MEM_TYPE_FAST would hand it out ahead of real
 * fast RAM half the time. Callers should prefer fast, then slow, then chip.
 */

/*
 * Flags. Bit 0 is what the ROM writes and what print_memory_map has always
 * tested for "[DMA]". The kernel's mem.h had these the other way round -
 * MEMF_TESTED on bit 0 - which was harmless only because nothing in the
 * kernel reads the flags yet. This is the ROM's actual behaviour.
 */
#define MEMF_DMA           (1 << 0)
#define MEMF_TESTED        (1 << 1)

struct mem_entry {
    unsigned long  base;
    unsigned long  size;
    unsigned short type;
    unsigned short flags;
};

/* MEMMAP_TABLE is 432 bytes: 36 entries including the terminator. */
#define MEMMAP_MAX_ENTRIES 36

/*
 * Carve [base, base+size) out of whichever free region contains it and mark
 * it MEM_TYPE_RESERVED, splitting that region as needed. Returns 0 on
 * success, -1 if the range is not inside a single entry or the table is
 * full.
 *
 * This is how the kernel's own image stops being handed out as free fast
 * RAM: the table is built before SYSTEM.BIN is loaded, so the size is not
 * known until afterwards.
 */
int memmap_reserve(struct mem_entry *map, unsigned long base,
                   unsigned long size);

#endif /* MEMMAP_H */
