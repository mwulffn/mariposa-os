/*
 * bitmap.h - the system bitmap standard
 *
 * docs/bitmap_design.md. One in-memory representation for every bitmap:
 * planar, rows interleaved, two strides, a format field with room to grow,
 * 24-bit colour above the hardware, half-open rectangles.
 */
#ifndef BITMAP_H
#define BITMAP_H

typedef unsigned long bitmap_t;         /* 0 is never a valid handle */
typedef unsigned long colour_t;         /* 0x00RRGGBB */

#define BITMAP_MAX_DEPTH    6

#define BMFMT_PLANAR        1           /* planar, rows interleaved */

#define BITMAP_DISPLAYABLE  0x0001      /* in chip RAM: can be shown and blitted */

/*
 * Rows are interleaved: the planes of row 0 side by side, then the planes
 * of row 1. So there are two strides, and they are different things:
 *
 *   bytes_per_row   how much data one row of one plane holds
 *   row_step        how far apart consecutive rows of the same plane are
 *
 * STEP ROWS BY row_step, NEVER BY bytes_per_row, and compute neither from
 * the width. A plane view has a row_step several times its bytes_per_row.
 */
struct bitmap_info {
    unsigned short width, height, depth;
    unsigned short format;              /* BMFMT_* */
    unsigned short flags;               /* BITMAP_* */
    unsigned short bytes_per_row;
    unsigned long  row_step;
    unsigned char *planes[BITMAP_MAX_DEPTH];    /* row 0 of each plane */
};

/* Origin top left, y down. Half-open: a pixel is inside if
 * x <= px < x + w. Signed, so that a rectangle may hang off any edge of a
 * bitmap - it is clipped, not refused. w or h <= 0 is empty. */
struct rect {
    short x, y, w, h;
};

void bitmap_init(void);

/* width a multiple of 16. Cleared. owner NULL is the kernel. 0 on failure. */
bitmap_t bitmap_alloc(unsigned long width, unsigned long height,
                      unsigned long depth, unsigned long flags, void *owner);
int      bitmap_free(bitmap_t bm);
void     bitmap_free_owner(void *owner);

int      bitmap_info(bitmap_t bm, struct bitmap_info *out);
void    *bitmap_owner(bitmap_t bm);     /* NULL for the kernel's, and for junk */

/* Describe one plane of bm as a 1-plane bitmap in its own right: same
 * memory, same row_step. What makes a plane usable as a mask. */
int      bitmap_plane_view(bitmap_t bm, unsigned long plane,
                           struct bitmap_info *out);

/* Set every pixel in r to a colour index, clipped to the bitmap. Every
 * plane is written, zeros as well as ones. By the CPU: the blitter's
 * version arrives with the blitter driver. */
void     bitmap_fill(bitmap_t bm, const struct rect *r, unsigned long index);

#endif /* BITMAP_H */
