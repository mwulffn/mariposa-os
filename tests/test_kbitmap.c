/*
 * test_kbitmap.c - src/kernel/bitmap.c: the system bitmap standard
 *
 * docs/bitmap_design.md, as assertions. The layout is an ABI - native code
 * will draw into these through bitmap_info() - so the descriptor's offsets
 * are spelled out here and not taken from the header.
 */
#include "protocol.h"

#include <stdint.h>
#include <string.h>

#define BITMAP_DISPLAYABLE 1u
#define BMFMT_PLANAR       1u

/* struct bitmap_info */
#define BI_WIDTH   0
#define BI_HEIGHT  2
#define BI_DEPTH   4
#define BI_FORMAT  6
#define BI_FLAGS   8
#define BI_BPR     10
#define BI_STEP    12   /* long */
#define BI_PLANES  16   /* six pointers */
#define BI_SIZE    40

#define CHIP_BASE  0x140000u
#define CHIP_SIZE  0x0A0000u
#define FAST_BASE   h_kernel_heap(0x40000u)
#define FAST_SIZE  0x040000u

static uint32_t kcall(const char *name, int nargs, const uint32_t *args)
{
    h_result r;
    int i;

    h_begin_call();
    for (i = nargs - 1; i >= 0; i--)
        h_push32(args[i]);
    r = h_call(h_sym(name));
    CHECK_CALL(r);
    return h_get_d(0);
}

static void put32(uint8_t *p, uint32_t v)
{ p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

static void setup(void)
{
    uint8_t map[36];
    uint32_t a[2];

    memset(map, 0, sizeof map);
    put32(map, CHIP_BASE);      put32(map + 4, CHIP_SIZE);  map[9]  = 1;
    put32(map + 12, FAST_BASE); put32(map + 16, FAST_SIZE); map[21] = 2;
    a[0] = h_alloc(map, sizeof map);
    a[1] = 0;
    kcall("kernel:_mem_init", 2, a);
    kcall("kernel:_bitmap_init", 0, NULL);
}

static uint32_t bm_alloc(uint32_t w, uint32_t h, uint32_t d, uint32_t flags)
{
    uint32_t a[5]; a[0] = w; a[1] = h; a[2] = d; a[3] = flags; a[4] = 0x1234;
    return kcall("kernel:_bitmap_alloc", 5, a);
}

static uint32_t info(uint32_t bm)
{
    uint8_t zero[BI_SIZE] = {0};
    uint32_t a[2];

    a[0] = bm; a[1] = h_alloc(zero, sizeof zero);
    CHECK_U32(0, kcall("kernel:_bitmap_info", 2, a));
    return a[1];
}

static int in_chip(uint32_t p) { return p >= CHIP_BASE && p < CHIP_BASE + CHIP_SIZE; }
static int in_fast(uint32_t p) { return p >= FAST_BASE && p < FAST_BASE + FAST_SIZE; }

/* Pixel (x, y) as the colour index the planes spell out. */
static unsigned pixel(uint32_t bi, int x, int y)
{
    uint32_t step = h_peek32(bi + BI_STEP);
    unsigned depth = h_peek16(bi + BI_DEPTH), p, v = 0;

    for (p = 0; p < depth; p++) {
        uint32_t plane = h_peek32(bi + BI_PLANES + 4 * p);
        if (h_peek8(plane + (uint32_t)y * step + (uint32_t)(x >> 3)) & (0x80 >> (x & 7)))
            v |= 1u << p;
    }
    return v;
}

/* --- layout ---------------------------------------------------------------- */

/* Rows interleaved: the planes of row 0, then the planes of row 1. */
static void t_rows_are_interleaved(void)
{
    uint32_t bi, p0;

    setup();
    bi = info(bm_alloc(640, 200, 3, BITMAP_DISPLAYABLE));

    CHECK_U32(640, h_peek16(bi + BI_WIDTH));
    CHECK_U32(200, h_peek16(bi + BI_HEIGHT));
    CHECK_U32(3, h_peek16(bi + BI_DEPTH));
    CHECK_U32(BMFMT_PLANAR, h_peek16(bi + BI_FORMAT));
    CHECK_U32(80, h_peek16(bi + BI_BPR));
    CHECK_U32(240, h_peek32(bi + BI_STEP));             /* depth * bytes_per_row */

    p0 = h_peek32(bi + BI_PLANES);
    CHECK_U32(p0 + 80,  h_peek32(bi + BI_PLANES + 4));  /* side by side in the row */
    CHECK_U32(p0 + 160, h_peek32(bi + BI_PLANES + 8));
    CHECK_U32(0, h_peek32(bi + BI_PLANES + 12));        /* no fourth plane */
}

static void t_new_bitmaps_are_clear(void)
{
    uint32_t bi, p0, i, dirty = 0;

    setup();
    /* make sure the memory it gets is not already zero */
    { uint32_t a[2]; uint32_t junk; a[0] = 40000; a[1] = 1;
      junk = kcall("kernel:_mem_alloc", 2, a);
      for (i = 0; i < 40000; i += 4) h_poke32(junk + i, 0xFFFFFFFFu);
      kcall("kernel:_mem_free", 1, &junk); }

    bi = info(bm_alloc(320, 100, 4, BITMAP_DISPLAYABLE));
    p0 = h_peek32(bi + BI_PLANES);
    for (i = 0; i < 40u * 4u * 100u; i++)
        if (h_peek8(p0 + i)) dirty++;
    CHECK(dirty == 0, "%u bytes of a new bitmap are not zero", dirty);
}

/* Displayable means chip RAM. Anything else goes where memory is plentiful. */
static void t_placement_follows_the_flag(void)
{
    uint32_t shown, stored;

    setup();
    shown  = info(bm_alloc(320, 64, 2, BITMAP_DISPLAYABLE));
    stored = info(bm_alloc(320, 64, 2, 0));

    CHECK(in_chip(h_peek32(shown + BI_PLANES)), "displayable bitmap is not in chip RAM");
    CHECK(in_fast(h_peek32(stored + BI_PLANES)), "plain bitmap took chip RAM");
    CHECK_U32(BITMAP_DISPLAYABLE, h_peek16(shown + BI_FLAGS));
    CHECK_U32(0, h_peek16(stored + BI_FLAGS));
}

/* One plane of any bitmap is a valid 1-plane bitmap: same rows, same step.
 * That is what two strides buy, and what lets a plane be a mask for free. */
static void t_a_plane_is_a_bitmap(void)
{
    uint8_t zero[BI_SIZE] = {0};
    uint32_t bm, whole, view, a[3];

    setup();
    bm = bm_alloc(320, 50, 4, 0);
    whole = info(bm);
    view = h_alloc(zero, sizeof zero);
    a[0] = bm; a[1] = 2; a[2] = view;
    CHECK_U32(0, kcall("kernel:_bitmap_plane_view", 3, a));

    CHECK_U32(1, h_peek16(view + BI_DEPTH));
    CHECK_U32(320, h_peek16(view + BI_WIDTH));
    CHECK_U32(40, h_peek16(view + BI_BPR));
    CHECK_U32(160, h_peek32(view + BI_STEP));           /* the parent's, not 40 */
    CHECK_U32(h_peek32(whole + BI_PLANES + 8), h_peek32(view + BI_PLANES));

    a[1] = 4;
    CHECK(kcall("kernel:_bitmap_plane_view", 3, a) != 0, "a view of plane 4 of 4");
}

/* --- geometry -------------------------------------------------------------- */

static void fill(uint32_t bm, int x, int y, int w, int h, uint32_t colour)
{
    uint8_t r[8];
    uint32_t a[3];

    r[0] = (uint8_t)(x >> 8); r[1] = (uint8_t)x;  r[2] = (uint8_t)(y >> 8); r[3] = (uint8_t)y;
    r[4] = (uint8_t)(w >> 8); r[5] = (uint8_t)w;  r[6] = (uint8_t)(h >> 8); r[7] = (uint8_t)h;
    a[0] = bm; a[1] = h_alloc(r, sizeof r); a[2] = colour;
    kcall("kernel:_bitmap_fill", 3, a);
}

/* Half-open: x <= px < x + w. The pixel at x + w belongs to the neighbour. */
static void t_rectangles_are_half_open(void)
{
    uint32_t bm, bi;

    setup();
    bm = bm_alloc(64, 32, 3, 0);
    bi = info(bm);
    fill(bm, 10, 5, 20, 8, 5);

    CHECK_U32(5, pixel(bi, 10, 5));                     /* first in */
    CHECK_U32(5, pixel(bi, 29, 12));                    /* last in */
    CHECK_U32(0, pixel(bi, 9, 5));
    CHECK_U32(0, pixel(bi, 30, 5));                     /* x + w is out */
    CHECK_U32(0, pixel(bi, 10, 4));
    CHECK_U32(0, pixel(bi, 10, 13));                    /* y + h is out */

    /* Neighbours share an edge and no pixels. */
    fill(bm, 30, 5, 10, 8, 2);
    CHECK_U32(5, pixel(bi, 29, 8));
    CHECK_U32(2, pixel(bi, 30, 8));
}

/* The colour is an index; its bits say which planes get ones - and which
 * get zeros, or filling over an old colour leaves some of it behind. */
static void t_fill_writes_every_plane(void)
{
    uint32_t bm, bi;

    setup();
    bm = bm_alloc(64, 16, 3, 0);
    bi = info(bm);
    fill(bm, 0, 0, 64, 16, 7);
    fill(bm, 8, 4, 16, 4, 2);
    CHECK_U32(2, pixel(bi, 12, 5));
    CHECK_U32(7, pixel(bi, 30, 5));
}

/* Off the edge is normal, not an error: clipped, never written outside. */
static void t_fill_is_clipped(void)
{
    uint32_t bm, bi, guard_before, guard_after, a[2], after;

    setup();
    /* a guard allocation either side, so a stray write has something to hit */
    a[0] = 64; a[1] = 2;
    guard_before = kcall("kernel:_mem_alloc", 2, a);
    bm = bm_alloc(32, 16, 2, 0);
    after = guard_after = kcall("kernel:_mem_alloc", 2, a);
    { uint32_t i; for (i = 0; i < 64; i += 4) { h_poke32(guard_before + i, 0xA5A5A5A5u); h_poke32(guard_after + i, 0xA5A5A5A5u); } }
    bi = info(bm);

    fill(bm, -10, -10, 20, 20, 3);                      /* top left corner */
    fill(bm, 25, 10, 100, 100, 1);                      /* bottom right */
    fill(bm, 100, 100, 5, 5, 3);                        /* wholly outside */
    fill(bm, 5, 5, 0, 10, 3);                           /* empty */
    fill(bm, 5, 5, -4, 4, 3);                           /* negative: empty */

    CHECK_U32(3, pixel(bi, 0, 0));
    CHECK_U32(3, pixel(bi, 9, 9));
    CHECK_U32(0, pixel(bi, 10, 10));
    CHECK_U32(1, pixel(bi, 31, 15));
    CHECK_U32(0, pixel(bi, 24, 10));
    CHECK_U32(0, pixel(bi, 5, 12));
    { uint32_t i; for (i = 0; i < 64; i += 4) {
        CHECK_U32(0xA5A5A5A5u, h_peek32(guard_before + i));
        CHECK_U32(0xA5A5A5A5u, h_peek32(after + i)); } }
    CHECK_U32(0, kcall("kernel:_mem_check", 0, NULL));
}

/* Edges that fall mid-byte, on both sides, and a fill inside one byte. */
static void t_fill_gets_the_edge_bits_right(void)
{
    uint32_t bm, bi;
    int x;

    setup();
    bm = bm_alloc(64, 4, 1, 0);
    bi = info(bm);
    fill(bm, 3, 0, 18, 1, 1);                           /* 3..20: across three bytes */
    fill(bm, 42, 1, 3, 1, 1);                           /* 42..44: inside one */

    for (x = 0; x < 32; x++)
        CHECK(pixel(bi, x, 0) == (unsigned)(x >= 3 && x < 21), "row 0, x=%d", x);
    for (x = 32; x < 64; x++)
        CHECK(pixel(bi, x, 1) == (unsigned)(x >= 42 && x < 45), "row 1, x=%d", x);
}

static const test_case tests[] = {
    { "rows_interleaved",       t_rows_are_interleaved,         NULL },
    { "new_bitmaps_clear",      t_new_bitmaps_are_clear,        NULL },
    { "placement_by_flag",      t_placement_follows_the_flag,   NULL },
    { "plane_is_a_bitmap",      t_a_plane_is_a_bitmap,          NULL },
    { "rect_half_open",         t_rectangles_are_half_open,     NULL },
    { "fill_every_plane",       t_fill_writes_every_plane,      NULL },
    { "fill_clipped",           t_fill_is_clipped,              NULL },
    { "fill_edge_bits",         t_fill_gets_the_edge_bits_right, NULL },
};

const test_suite kbitmap_suite = { "kbmp", tests, sizeof tests / sizeof tests[0] };
