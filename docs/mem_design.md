# Memory System Design

> **Status.** Implemented in `src/kernel/mem.c` and pinned by the `kmem.*`
> tests, which run it out of the real kernel image. Where the code differs
> from the text below it is deliberate:
>
> - **The free list is doubly linked**, not singly. Coalescing unlinks
>   arbitrary blocks, and with one link that is a walk of the whole list for
>   every free. The links live in the free block's own payload, so they cost
>   a used block nothing.
> - **There is a third pool, slow RAM** (`$C00000`), first-fit like fast.
>   `ALLOC_ANY` tries fast, then slow, then chip.
> - **A pool can span several regions** of the memory map. Each is closed by
>   a zero-length marker block, so coalescing never crosses between them.
> - **The API is `mem_alloc(size, flags)` / `mem_free(ptr)`**, with
>   `kmalloc`, `kfree`, `chip_alloc`, `chip_free` and `chip_largest_free` as
>   macros over it. `mem_free` finds the pool itself.
> - **`mem_free` refuses bad pointers** - outside every pool, misaligned,
>   mid-block, freed twice, or with a trampled header - and returns -1
>   without touching the heap. The pool lookup happens before the header is
>   read, so a wild pointer is never dereferenced.
> - **`mem_check()`** walks every pool and verifies it. The header's magic
>   sits directly after the previous block's payload, so it doubles as an
>   overrun canary.
> - **Ownership is an opaque tag**: `mem_alloc_tagged(size, flags, owner)`
>   and `mem_free_owner(owner)`. It becomes a task pointer once tasks exist;
>   0 is the kernel and is never bulk-freed.
> - Every entry point is a critical section, per
>   `docs/interrupt_control_design.md`.
>
> Header cost is 16 bytes per allocation; the smallest block is 24.

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


