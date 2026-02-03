# Memory System Design

## Overview

Two separate heaps using the same free-list allocator with coalescing. Chip RAM uses best-fit strategy to minimize fragmentation. Fast RAM uses first-fit for speed.

## Hardware Constraints

| Resource | Size | Properties |
|----------|------|------------|
| Chip RAM | 1 MB | DMA-accessible, shared with custom chips |
| Fast RAM | 8 MB | CPU-only, starts at $200000 |

## Chip RAM

**Purpose:** Graphics resources only. Bitmaps, sprites, copper lists, audio buffers.

**Strategy:** Best-fit with coalescing.

**Access:** Kernel only. Applications never allocate chip RAM directly. The gfx subsystem allocates on their behalf and tracks ownership per task.

**Key queries:**
- Total free
- Largest contiguous free block (determines whether a screen can be opened)

**Fragmentation is the primary concern.** Best-fit preserves large contiguous blocks for big bitmap allocations. Coalescing on free merges adjacent blocks immediately.

## Fast RAM

**Purpose:** Kernel objects, Wasm VM memory, general-purpose kernel allocations.

**Strategy:** First-fit with coalescing.

**Access:** Kernel allocates directly. Applications get memory through Wasm linear memory (single large block per VM).

**Layout after boot:**

```
$200000  ┌──────────────────────────┐
         │ Kernel image (code+data) │
         ├──────────────────────────┤
         │ Fast RAM heap            │
         │ (managed by allocator)   │
         │                          │
$9FFFFF  └──────────────────────────┘
```

## Allocator Structure

Both heaps use the same underlying data structure:

**Block header:** Contains size, free flag, physical neighbors (prev/next), free list linkage.

**Free list:** Singly-linked chain of free blocks.

**Coalescing:** On free, merge with physically adjacent free blocks in both directions. Prevents long-term fragmentation.

**Splitting:** On alloc, if the chosen block is significantly larger than requested, split it and return the remainder to the free list.

**Alignment:** All allocations aligned to 8 bytes minimum.

## API

### Chip RAM (kernel internal, not exposed to applications)

```
chip_alloc(size)         → pointer or NULL
chip_free(ptr)
chip_avail()             → total free bytes
chip_largest_free()      → largest contiguous block
```

### Fast RAM (kernel use)

```
kmalloc(size)            → pointer or NULL
kfree(ptr)
fast_avail()             → total free bytes
```

### Application-facing (through subsystems)

Applications never call allocators directly.

- **Graphics:** `gfx_alloc_bitmap()`, `gfx_open_screen()` etc. allocate chip RAM internally.
- **Wasm VM:** Loader allocates a single large fast RAM block per VM. The VM manages its own linear memory internally via sbrk.
- **Native apps (hunk):** Loader allocates fast RAM for code/data/bss hunks.

## Resource Tracking

Every allocation in both heaps is tagged with the owning task. On task exit, all memory owned by that task is freed automatically. This prevents memory leaks from crashed or misbehaving applications.

Kernel-owned allocations use a NULL or sentinel task tag and are never auto-freed.


