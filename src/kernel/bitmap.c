/*
 * bitmap.c - the system bitmap standard
 *
 * See bitmap.h and docs/bitmap_design.md.
 */
#include "bitmap.h"
#include "mem.h"
#include "cpu.h"

#define MAX_BITMAPS 32

static struct {
    struct bitmap_info info;
    void              *owner;
    unsigned char      used;
    unsigned char      generation;  /* so a stale handle is not a valid one */
} bitmaps[MAX_BITMAPS];

void bitmap_init(void)
{
    int i;

    for (i = 0; i < MAX_BITMAPS; i++)
        bitmaps[i].used = 0;
}

/* handle = generation << 8 | slot + 1 */
static bitmap_t handle_of(int slot)
{
    return ((bitmap_t)bitmaps[slot].generation << 8) | (unsigned long)(slot + 1);
}

static int slot_of(bitmap_t bm)
{
    unsigned long slot = (bm & 0xFF) - 1;

    if (bm == 0 || slot >= MAX_BITMAPS || !bitmaps[slot].used ||
        bitmaps[slot].generation != ((bm >> 8) & 0xFF))
        return -1;
    return (int)slot;
}

bitmap_t bitmap_alloc(unsigned long width, unsigned long height,
                      unsigned long depth, unsigned long flags, void *owner)
{
    unsigned long bpr, step, size, i;
    unsigned char *mem, *plane;
    int slot;

    if (width == 0 || width > 2048 || (width & 15) || height == 0 ||
        height > 2048 || depth == 0 || depth > BITMAP_MAX_DEPTH)
        return 0;

    bpr = width / 8;
    step = 0;
    for (i = 0; i < depth; i++)
        step += bpr;
    size = 0;
    for (i = 0; i < height; i++)        /* no multiply: see docs/display_design.md */
        size += step;

    mem = mem_alloc_tagged(size, (flags & BITMAP_DISPLAYABLE) ? ALLOC_CHIP : ALLOC_ANY,
                           owner);
    if (!mem)
        return 0;
    for (i = 0; i < size; i++)
        mem[i] = 0;

    CRITICAL_ENTER();
    for (slot = 0; slot < MAX_BITMAPS && bitmaps[slot].used; slot++)
        ;
    if (slot < MAX_BITMAPS) {
        struct bitmap_info *bi = &bitmaps[slot].info;

        bitmaps[slot].used = 1;
        bitmaps[slot].generation++;
        bitmaps[slot].owner = owner;
        bi->width  = (unsigned short)width;
        bi->height = (unsigned short)height;
        bi->depth  = (unsigned short)depth;
        bi->format = BMFMT_PLANAR;
        bi->flags  = (unsigned short)(flags & BITMAP_DISPLAYABLE);
        bi->bytes_per_row = (unsigned short)bpr;
        bi->row_step = step;
        plane = mem;
        for (i = 0; i < BITMAP_MAX_DEPTH; i++) {
            bi->planes[i] = i < depth ? plane : 0;
            plane += bpr;               /* planes side by side in the row */
        }
    }
    CRITICAL_EXIT();

    if (slot == MAX_BITMAPS) {
        mem_free(mem);
        return 0;
    }
    return handle_of(slot);
}

int bitmap_free(bitmap_t bm)
{
    unsigned char *mem = 0;
    int slot;

    CRITICAL_ENTER();
    slot = slot_of(bm);
    if (slot >= 0) {
        mem = bitmaps[slot].info.planes[0];     /* the start of the allocation */
        bitmaps[slot].used = 0;
    }
    CRITICAL_EXIT();

    /* A bitmap still on screen goes on being read by bitplane DMA after
     * this. That is reading freed memory, which shows garbage and harms
     * nothing; the owner's next display program puts it right. */
    if (!mem)
        return -1;
    mem_free(mem);
    return 0;
}

void bitmap_free_owner(void *owner)
{
    int i;

    for (i = 0; i < MAX_BITMAPS; i++) {
        bitmap_t bm = 0;

        CRITICAL_ENTER();
        if (bitmaps[i].used && bitmaps[i].owner == owner)
            bm = handle_of(i);
        CRITICAL_EXIT();
        if (bm)
            bitmap_free(bm);
    }
}

int bitmap_info(bitmap_t bm, struct bitmap_info *out)
{
    int slot, rc = -1;

    CRITICAL_ENTER();
    slot = slot_of(bm);
    if (slot >= 0) {
        *out = bitmaps[slot].info;
        rc = 0;
    }
    CRITICAL_EXIT();
    return rc;
}

void *bitmap_owner(bitmap_t bm)
{
    int slot = slot_of(bm);

    return slot >= 0 ? bitmaps[slot].owner : 0;
}

int bitmap_plane_view(bitmap_t bm, unsigned long plane, struct bitmap_info *out)
{
    unsigned char *start;
    int i;

    if (bitmap_info(bm, out) != 0 || plane >= out->depth)
        return -1;

    /* Same rows, same distance between them; just the one plane. */
    start = out->planes[plane];
    out->depth = 1;
    out->planes[0] = start;
    for (i = 1; i < BITMAP_MAX_DEPTH; i++)
        out->planes[i] = 0;
    return 0;
}

/* ----------------------------------------------------------------- fill --- */

/* Bits from..to-1 of a row, set or cleared. Whole bytes in the middle, a
 * mask at each end - or one mask, if both ends fall in the same byte. */
static void fill_run(unsigned char *row, int from, int to, int set)
{
    unsigned char *p = row + (from >> 3), *last = row + ((to - 1) >> 3);
    unsigned char first_mask = (unsigned char)(0xFF >> (from & 7));
    unsigned char last_mask  = (unsigned char)(0xFF << (7 - ((to - 1) & 7)));

    if (p == last) {
        first_mask &= last_mask;
        *p = set ? (*p | first_mask) : (*p & (unsigned char)~first_mask);
        return;
    }
    *p = set ? (*p | first_mask) : (*p & (unsigned char)~first_mask);
    for (p++; p < last; p++)
        *p = set ? 0xFF : 0x00;
    *p = set ? (*p | last_mask) : (*p & (unsigned char)~last_mask);
}

void bitmap_fill(bitmap_t bm, const struct rect *r, unsigned long index)
{
    struct bitmap_info bi;
    long x0 = r->x, y0 = r->y, x1, y1, y;
    unsigned int p;

    if (bitmap_info(bm, &bi) != 0 || r->w <= 0 || r->h <= 0)
        return;

    /* Clip. Half-open throughout, so x1 and y1 are one past the last. */
    x1 = x0 + r->w;
    y1 = y0 + r->h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > bi.width)  x1 = bi.width;
    if (y1 > bi.height) y1 = bi.height;
    if (x0 >= x1 || y0 >= y1)
        return;

    for (p = 0; p < bi.depth; p++) {
        unsigned char *row = bi.planes[p];
        int set = (int)((index >> p) & 1);

        for (y = 0; y < y0; y++)
            row += bi.row_step;
        for (; y < y1; y++) {
            fill_run(row, (int)x0, (int)x1, set);
            row += bi.row_step;
        }
    }
}
