/*
 * display.h - the display hardware, and who owns it
 *
 * Mechanism only: docs/display_design.md. The kernel owns the copper, the
 * bitplane pointers and chip RAM bitmaps because there is one of each and
 * because DMA pointed at the wrong place writes anywhere in chip RAM. What
 * is shown, how it is laid out and what a window is belong to whoever
 * acquires the display.
 *
 * Nothing here takes or returns a pointer into kernel state. Bitmaps are
 * handles and every argument is checked, so the same calls can sit behind a
 * library jump table or a Wasm host call without being rewritten.
 */
#ifndef DISPLAY_H
#define DISPLAY_H

/* ---------------------------------------------------------------- bitmaps --- */

typedef unsigned long bitmap_t;         /* 0 is never a valid handle */

#define BITMAP_MAX_DEPTH  6

struct bitmap_info {
    unsigned short width, height, depth;
    unsigned short bytes_per_row;
    unsigned char *planes[BITMAP_MAX_DEPTH];    /* chip RAM */
};

/* width a multiple of 16. NULL owner is the kernel. 0 if it cannot be had. */
bitmap_t bitmap_alloc(unsigned long width, unsigned long height,
                      unsigned long depth, void *owner);
int      bitmap_free(bitmap_t bm);
int      bitmap_info(bitmap_t bm, struct bitmap_info *out);

/* ------------------------------------------------------ the display program --- */

/*
 * What the hardware can vary down the screen, and only down it: the copper
 * works by scanline. A band is a run of scanlines showing one bitmap in one
 * mode with one palette. Side by side is not something the chipset can do
 * with two bitmaps, so it is not something a band can say.
 *
 * y counts from the top of the visible display. Bands must be in order and
 * must leave DISPLAY_BAND_GAP blank lines between them: the copper manages
 * about fifteen register writes before a line starts being drawn, a band
 * needs more than that to set up, and the only place to do it without it
 * showing is a line where nothing is displayed. The gap shows `background`.
 *
 * yoffset is the bitmap row shown on the band's first line, and it wraps: a
 * band may be a window onto a circular bitmap, which is how text scrolls
 * without moving a byte.
 */
#define DISPLAY_LINES      256          /* PAL */
#define DISPLAY_MAX_BANDS  8
#define DISPLAY_BAND_GAP   2

#define BAND_HIRES         0x0001       /* 640 across; else 320 */

struct display_band {
    unsigned short y, height;
    bitmap_t       bitmap;
    unsigned short yoffset;
    unsigned short flags;
    unsigned short ncolors;             /* how many of colors[] to load */
    unsigned short reserved;
    unsigned short colors[32];          /* $0RGB */
};

/* ------------------------------------------------------------- ownership --- */

/*
 * One owner at a time, and a stack of them. Acquiring takes the display from
 * whoever has it; they are told (DISPLAY_LOST), keep their bitmaps, and may
 * keep drawing into them unseen. Releasing gives it back exactly as it was
 * (DISPLAY_RESTORED). The kernel's boot console is the bottom of the stack,
 * so it is what appears when everyone else has gone - including on a crash.
 *
 * DISPLAY_RAW is for the owner that wants the bare hardware: a game. The
 * kernel stops touching the copper, the bitplane registers and DMACON for as
 * long as that owner is on top, and puts all of it back on release. That is
 * taking over the machine in a way the system can recover from.
 */
#define DISPLAY_RAW        0x0001

#define DISPLAY_LOST       1
#define DISPLAY_RESTORED   2

typedef void (*display_notify_fn)(void *owner, unsigned long what);

void display_init(void);

/* owner is opaque and must be non-NULL and not already on the stack. */
int  display_acquire(void *owner, unsigned long flags, display_notify_fn notify);
int  display_release(void *owner);
void *display_owner(void);

/* owner is gone: release the display if it holds it and free every bitmap
 * it owns. Called for every exiting task; harmless for one that owns
 * nothing. */
void display_owner_gone(void *owner);

/* Replace owner's program. Takes effect at the next vertical blank if owner
 * is on top, and is remembered for when it is again if not. -1, changing
 * nothing, if any band is unacceptable or owner is not on the stack. */
int  display_set_program(void *owner, const struct display_band *bands,
                         unsigned long nbands, unsigned long background);

/* Sleep until the next vertical blank. Task context only. */
void display_wait_vblank(void);

/* From the vertical blank handler. */
void display_vblank(void);

#endif /* DISPLAY_H */
