/*
 * test_kdisplay.c - display.c and dcon.c: bitmaps, bands, ownership, console
 *
 * The copper list is checked by running it, not by reading it: h_copper_at()
 * is a copper, and the question a test asks is "what is the chipset showing
 * at raster line N" - which bitmap row, how many planes, which colours. How
 * the list gets there is display.c's business.
 *
 * What none of this can check is horizontal timing - whether a band's setup
 * really fits in the gap above it. That takes a real chipset, or FS-UAE and
 * a pair of eyes.
 */
#include "protocol.h"

#include <stdint.h>
#include <string.h>

#define R_COP1LCH  0x080
#define R_COP1LCL  0x082
#define R_DMACON   0x096
#define R_BPL1PTH  0x0E0
#define R_BPL2PTH  0x0E4
#define R_BPLCON0  0x100
#define R_BPL1MOD  0x108
#define R_BPL2MOD  0x10A
#define R_COLOR00  0x180

#define FIRST_LINE 0x2C
#define GAP        2
#define BAND_HIRES 1u
#define DISPLAY_RAW 1u

#define CHIP_BASE  0x140000u
#define CHIP_SIZE  0x0A0000u
#define FAST_BASE  0x210000u
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
    uint32_t a[2], cpu = 0;

    memset(map, 0, sizeof map);
    put32(map, CHIP_BASE);      put32(map + 4, CHIP_SIZE);  map[9]  = 1;
    put32(map + 12, FAST_BASE); put32(map + 16, FAST_SIZE); map[21] = 2;
    a[0] = h_alloc(map, sizeof map);
    a[1] = 0;
    kcall("kernel:_mem_init", 2, a);
    kcall("kernel:_ser_init", 0, NULL);
    kcall("kernel:_sched_init", 1, &cpu);
    kcall("kernel:_irq_init", 0, NULL);
    kcall("kernel:_display_init", 0, NULL);
}

/* A vertical blank: what swaps a finished copper list in. */
static void vblank(void)
{
    static const uint8_t nops[] = { 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x75 };
    h_result r;

    h_raise(H_INTF_VERTB);
    h_begin_call();
    h_set_sr(0x2000);
    r = h_call(h_alloc(nops, sizeof nops));
    CHECK_CALL(r);
    h_set_sr(0x2700);
}

static uint32_t copper_list(void)
{
    return ((uint32_t)h_custom(R_COP1LCH) << 16) | h_custom(R_COP1LCL);
}

typedef struct { uint16_t bplcon0, colors[4], mod1, mod2; uint32_t bpl1, bpl2; } shown;

static shown at_line(int display_line)
{
    uint16_t regs[0x100];
    shown s;
    int i;

    memset(&s, 0, sizeof s);
    if (h_copper_at(copper_list(), FIRST_LINE + display_line, regs) != 0) {
        t_fail("copper list at $%X never ends", copper_list());
        return s;
    }
    s.bplcon0 = regs[R_BPLCON0 >> 1];
    s.mod1 = regs[R_BPL1MOD >> 1];
    s.mod2 = regs[R_BPL2MOD >> 1];
    s.bpl1 = ((uint32_t)regs[R_BPL1PTH >> 1] << 16) | regs[(R_BPL1PTH + 2) >> 1];
    s.bpl2 = ((uint32_t)regs[R_BPL2PTH >> 1] << 16) | regs[(R_BPL2PTH + 2) >> 1];
    for (i = 0; i < 4; i++)
        s.colors[i] = regs[(R_COLOR00 >> 1) + i];
    return s;
}

static int planes_on(const shown *s) { return (s->bplcon0 >> 12) & 7; }

/* A test writes colours the way the copper will show them, $0RGB, because
 * that is what it then looks for. The kernel takes 24-bit. */
static uint32_t rgb24(uint16_t ocs)
{
    return ((uint32_t)((ocs >> 8) & 15) * 0x11u << 16) |
           ((uint32_t)((ocs >> 4) & 15) * 0x11u << 8) | ((uint32_t)(ocs & 15) * 0x11u);
}

/* struct display_band, 144 bytes: y, height, bitmap, yoffset, flags,
 * ncolors, reserved, then colors[32] as longs from offset 16. */
#define BAND_SIZE 144
typedef struct { unsigned y, height; uint32_t bitmap; unsigned yoffset, flags; uint16_t c1; } band;

static uint32_t program24(uint32_t owner, const band *b, int n, uint32_t background24,
                          uint32_t c1_24)
{
    uint8_t buf[BAND_SIZE * 9];
    uint32_t a[4];
    int i;

    memset(buf, 0, sizeof buf);
    for (i = 0; i < n; i++) {
        uint8_t *p = buf + BAND_SIZE * i;
        p[0] = (uint8_t)(b[i].y >> 8);       p[1] = (uint8_t)b[i].y;
        p[2] = (uint8_t)(b[i].height >> 8);  p[3] = (uint8_t)b[i].height;
        put32(p + 4, b[i].bitmap);
        p[8] = (uint8_t)(b[i].yoffset >> 8); p[9] = (uint8_t)b[i].yoffset;
        p[11] = (uint8_t)b[i].flags;
        p[13] = 4;                                          /* ncolors */
        put32(p + 20, c1_24 ? c1_24 : rgb24(b[i].c1));      /* colors[1] */
    }
    a[0] = owner; a[1] = h_alloc(buf, (size_t)BAND_SIZE * (unsigned)(n ? n : 1));
    a[2] = (uint32_t)n; a[3] = background24;
    return kcall("kernel:_display_set_program", 4, a);
}

static uint32_t program(uint32_t owner, const band *b, int n, uint32_t background)
{
    return program24(owner, b, n, rgb24((uint16_t)background), 0);
}

#define BITMAP_DISPLAYABLE 1u

static uint32_t bm_alloc_flags(uint32_t w, uint32_t h, uint32_t d, uint32_t flags, uint32_t owner)
{
    uint32_t a[5]; a[0] = w; a[1] = h; a[2] = d; a[3] = flags; a[4] = owner;
    return kcall("kernel:_bitmap_alloc", 5, a);
}

static uint32_t bm_alloc(uint32_t w, uint32_t h, uint32_t d, uint32_t owner)
{
    return bm_alloc_flags(w, h, d, BITMAP_DISPLAYABLE, owner);
}

/* struct bitmap_info: the planes are six pointers from offset 16. See
 * test_kbitmap.c, which pins the whole layout. */
static uint32_t plane(uint32_t bm, int n)
{
    uint8_t zero[40] = {0};
    uint32_t a[2];

    a[0] = bm; a[1] = h_alloc(zero, sizeof zero);
    CHECK_U32(0, kcall("kernel:_bitmap_info", 2, a));
    return h_peek32(a[1] + 16 + 4u * (unsigned)n);
}

static uint32_t acquire(uint32_t owner, uint32_t flags, const char *notify)
{
    uint32_t a[3]; a[0] = owner; a[1] = flags; a[2] = notify ? h_sym(notify) : 0;
    return kcall("kernel:_display_acquire", 3, a);
}

static uint32_t release(uint32_t owner) { return kcall("kernel:_display_release", 1, &owner); }

static uint32_t new_owner(void)
{
    uint8_t zero[16] = {0};
    return h_alloc(zero, sizeof zero);
}

/* --- bitmaps --------------------------------------------------------------- */

static void t_bitmaps_live_in_chip_ram(void)
{
    uint32_t pool = 1, before, bm, p0, p1;

    setup();
    before = kcall("kernel:_mem_avail", 1, &pool);
    bm = bm_alloc(640, 256, 2, 0x1234);
    CHECK(bm != 0, "no bitmap");
    p0 = plane(bm, 0);  p1 = plane(bm, 1);
    CHECK(p0 >= CHIP_BASE && p0 < CHIP_BASE + CHIP_SIZE, "plane 0 at $%X is not chip RAM", p0);
    CHECK_U32(80, p1 - p0);                             /* interleaved by row */
    CHECK_U32(0, plane(bm, 2));

    CHECK_U32(0, kcall("kernel:_bitmap_free", 1, &bm));
    CHECK_U32(before, kcall("kernel:_mem_avail", 1, &pool));
    CHECK(kcall("kernel:_bitmap_free", 1, &bm) != 0, "freed twice");
}

static void t_bitmap_nonsense_is_refused(void)
{
    setup();
    CHECK_U32(0, bm_alloc(0, 256, 2, 1));
    CHECK_U32(0, bm_alloc(650, 256, 2, 1));             /* not a multiple of 16 */
    CHECK_U32(0, bm_alloc(640, 256, 0, 1));
    CHECK_U32(0, bm_alloc(640, 256, 7, 1));
    CHECK_U32(0, bm_alloc(2048, 2048, 6, 1));           /* more chip RAM than there is */
}

/* A handle that has been freed must not come back to life when its slot is
 * reused for somebody else's bitmap. */
static void t_stale_handle_stays_dead(void)
{
    uint32_t old, fresh;

    setup();
    old = bm_alloc(320, 64, 1, 1);
    kcall("kernel:_bitmap_free", 1, &old);
    fresh = bm_alloc(320, 64, 1, 2);
    CHECK(fresh != old, "handle reused: $%X", old);
    CHECK(kcall("kernel:_bitmap_free", 1, &old) != 0, "stale handle freed the new bitmap");
    CHECK(plane(fresh, 0) != 0, "new bitmap gone");
}

/* --- the display program --------------------------------------------------- */

static void t_one_band(void)
{
    uint32_t me = new_owner(), bm;
    band b = { 10, 200, 0, 0, BAND_HIRES, 0x0FFF };
    shown s;

    setup();
    bm = b.bitmap = bm_alloc(640, 256, 2, me);
    CHECK_U32(0, acquire(me, 0, NULL));
    CHECK_U32(0, program(me, &b, 1, 0x005A));
    vblank();

    s = at_line(5);                                     /* above the band */
    CHECK_U32(0, planes_on(&s));
    CHECK_U32(0x005A, s.colors[0]);

    s = at_line(100);
    CHECK_U32(2, planes_on(&s));
    CHECK(s.bplcon0 & 0x8000, "hires not set: BPLCON0 $%04X", s.bplcon0);
    CHECK_U32(plane(bm, 0), s.bpl1);
    CHECK_U32(plane(bm, 1), s.bpl2);
    CHECK_U32(0x0FFF, s.colors[1]);

    s = at_line(220);                                   /* below it */
    CHECK_U32(0, planes_on(&s));
}

/* Two bands, two bitmaps, two palettes: what the copper is for. */
static void t_two_bands_two_palettes(void)
{
    uint32_t me = new_owner();
    band b[2] = { { 0, 100, 0, 0, BAND_HIRES, 0x0F00 }, { 110, 100, 0, 0, 0, 0x00F0 } };
    shown s;

    setup();
    b[0].bitmap = bm_alloc(640, 100, 2, me);
    b[1].bitmap = bm_alloc(320, 100, 3, me);
    acquire(me, 0, NULL);
    CHECK_U32(0, program(me, b, 2, 0));
    vblank();

    s = at_line(50);
    CHECK_U32(0x0F00, s.colors[1]);  CHECK_U32(2, planes_on(&s));
    CHECK_U32(plane(b[0].bitmap, 0), s.bpl1);

    s = at_line(104);                                   /* the gap */
    CHECK_U32(0, planes_on(&s));

    s = at_line(150);
    CHECK_U32(0x00F0, s.colors[1]);  CHECK_U32(3, planes_on(&s));
    CHECK(!(s.bplcon0 & 0x8000), "second band should be lores");
    CHECK_U32(plane(b[1].bitmap, 0), s.bpl1);
}

/*
 * Rows are interleaved, so after fetching a row the bitplane pointers have
 * to skip the other planes to reach their own next row: that is the modulo.
 * Get it wrong and every line after the first shows another plane's data.
 */
static void t_modulo_skips_the_other_planes(void)
{
    uint32_t me = new_owner();
    band b[2] = { { 0, 100, 0, 0, BAND_HIRES, 0 }, { 110, 100, 0, 0, 0, 0 } };
    shown s;

    setup();
    b[0].bitmap = bm_alloc(640, 100, 4, me);            /* 80 shown, step 320 */
    b[1].bitmap = bm_alloc(640, 100, 3, me);            /* lores window on a wide bitmap */
    acquire(me, 0, NULL);
    CHECK_U32(0, program(me, b, 2, 0));
    vblank();

    s = at_line(50);
    CHECK_U32(3 * 80, s.mod1);  CHECK_U32(3 * 80, s.mod2);
    s = at_line(150);
    CHECK_U32(3 * 80 - 40, s.mod1);                     /* 40 fetched of a 240 step */
    CHECK_U32(plane(b[1].bitmap, 1), s.bpl2);
    CHECK_U32(plane(b[1].bitmap, 0) + 80, s.bpl2);      /* next to plane 0, not after it */
}

/* Colours above the hardware are 24-bit. OCS gets the top four bits a gun. */
static void t_colours_are_quantised_for_ocs(void)
{
    uint32_t me = new_owner();
    band b = { 0, 100, 0, 0, BAND_HIRES, 0 };
    shown s;

    setup();
    b.bitmap = bm_alloc(640, 100, 2, me);
    acquire(me, 0, NULL);
    CHECK_U32(0, program24(me, &b, 1, 0x00F8F8F8u, 0x00123456u));
    vblank();

    s = at_line(50);
    CHECK_U32(0x0FFF, s.colors[0]);                     /* $F8 is $F, not $10 */
    CHECK_U32(0x0135, s.colors[1]);
}

/* The copper's vertical counter is eight bits and PAL has more lines than
 * that. A band down here is reached through the $FFDF wait. */
static void t_band_below_raster_line_255(void)
{
    uint32_t me = new_owner();
    band b = { 220, 36, 0, 0, BAND_HIRES, 0x0ABC };
    shown s;

    setup();
    b.bitmap = bm_alloc(640, 36, 1, me);
    acquire(me, 0, NULL);
    CHECK_U32(0, program(me, &b, 1, 0));
    vblank();

    s = at_line(215);  CHECK_U32(0, planes_on(&s));
    s = at_line(240);  CHECK_U32(1, planes_on(&s));  CHECK_U32(0x0ABC, s.colors[1]);
    s = at_line(255);  CHECK_U32(1, planes_on(&s));
}

/* A band is a window onto a circular bitmap: start partway down, and where
 * it runs off the bottom it carries on from the top. */
static void t_yoffset_wraps(void)
{
    uint32_t me = new_owner(), bm;
    band b = { 0, 256, 0, 100, BAND_HIRES, 0x0FFF };
    shown s;

    setup();
    bm = b.bitmap = bm_alloc(640, 256, 2, me);
    acquire(me, 0, NULL);
    CHECK_U32(0, program(me, &b, 1, 0));
    vblank();

    s = at_line(0);
    CHECK_U32(plane(bm, 0) + 100u * 160u, s.bpl1);      /* row 100 on top: rows are
                                                         * row_step apart, 2 x 80 */
    s = at_line(155);
    CHECK_U32(plane(bm, 0) + 100u * 160u, s.bpl1);      /* not reloaded yet */
    s = at_line(156);
    CHECK_U32(plane(bm, 0), s.bpl1);                    /* row 0 follows row 255 */
}

static void t_bad_programs_are_refused(void)
{
    uint32_t me = new_owner(), them = new_owner(), mine, theirs, lores;
    band good = { 0, 100, 0, 0, BAND_HIRES, 0 };
    band b[2];

    setup();
    mine   = bm_alloc(640, 100, 2, me);
    theirs = bm_alloc(640, 100, 2, them);
    lores  = bm_alloc(320, 100, 2, me);
    acquire(me, 0, NULL);
    good.bitmap = mine;
    CHECK_U32(0, program(me, &good, 1, 0x0123));
    vblank();

#define REFUSED(what) CHECK(program(me, b, 2, 0) != 0, what " was accepted")
    b[0] = good; b[1] = good; b[1].y = 50;              REFUSED("overlapping bands");
    b[0] = good; b[1] = good; b[1].y = 100 + GAP - 1;   REFUSED("bands with no gap between");
    b[0] = good; b[1] = good; b[1].y = 200;             REFUSED("a band past the bottom");
    b[0] = good; b[1] = good; b[1].y = 120; b[1].bitmap = theirs;
                                                        REFUSED("someone else's bitmap");
    b[0] = good; b[1] = good; b[1].y = 120; b[1].bitmap = 0xDEAD;
                                                        REFUSED("a handle that was never issued");
    b[0] = good; b[1] = good; b[1].y = 120; b[1].bitmap = lores;
                                                        REFUSED("a 320 wide bitmap in hires");
    b[0] = good; b[1] = good; b[1].y = 120; b[1].bitmap = bm_alloc_flags(640, 100, 2, 0, me);
                                                        REFUSED("a bitmap that is not in chip RAM");
    b[0] = good; b[1] = good; b[1].y = 120; b[1].yoffset = 100;
                                                        REFUSED("a yoffset past the bitmap");
    CHECK(program(them, &good, 1, 0) != 0, "a program from someone not on the stack");

    /* And a refusal changes nothing. */
    vblank();
    { shown s = at_line(50); CHECK_U32(plane(mine, 0), s.bpl1); CHECK_U32(0x0123, s.colors[0]); }
}

/* A list must not change under the copper: COP1LC moves in the vertical
 * blank and nowhere else. */
static void t_swap_waits_for_vblank(void)
{
    uint32_t me = new_owner(), first;
    band b = { 0, 100, 0, 0, BAND_HIRES, 0 };
    unsigned writes;

    setup();
    b.bitmap = bm_alloc(640, 100, 1, me);
    acquire(me, 0, NULL);
    vblank();
    first = copper_list();
    writes = h_custom_writes(R_COP1LCH);

    CHECK_U32(0, program(me, &b, 1, 0));
    CHECK_U32(writes, h_custom_writes(R_COP1LCH));
    CHECK_U32(first, copper_list());

    vblank();
    CHECK(copper_list() != first, "new list not swapped in");
    { shown s = at_line(50); CHECK_U32(1, planes_on(&s)); }

    vblank();                                           /* nothing pending: */
    CHECK_U32(writes + 1, h_custom_writes(R_COP1LCH));  /* nothing written */
}

/* --- ownership ------------------------------------------------------------- */

static void t_ownership_is_a_stack(void)
{
    uint32_t server = new_owner(), game = new_owner(), sbm, gbm;
    band sb = { 0, 100, 0, 0, BAND_HIRES, 0x0111 };
    band gb = { 0, 200, 0, 0, 0, 0x0222 };
    shown s;

    setup();
    sbm = sb.bitmap = bm_alloc(640, 100, 2, server);
    gbm = gb.bitmap = bm_alloc(320, 200, 4, game);

    acquire(server, 0, "notify_record");
    program(server, &sb, 1, 0);
    vblank();
    s = at_line(50);  CHECK_U32(plane(sbm, 0), s.bpl1);

    /* The game asks; the server is told and keeps what it has. */
    CHECK_U32(0, acquire(game, 0, "notify_record"));
    CHECK_U32(1, h_peek32(server + 4));                 /* DISPLAY_LOST */
    CHECK_U32(game, kcall("kernel:_display_owner", 0, NULL));
    program(game, &gb, 1, 0);
    vblank();
    s = at_line(50);  CHECK_U32(plane(gbm, 0), s.bpl1);  CHECK_U32(4, planes_on(&s));

    /* The server may go on changing its program. Nobody sees it yet. */
    sb.c1 = 0x0333;
    CHECK_U32(0, program(server, &sb, 1, 0));
    vblank();
    s = at_line(50);  CHECK_U32(plane(gbm, 0), s.bpl1);

    /* The game leaves: the server's display comes back, as it now is. */
    CHECK_U32(0, release(game));
    CHECK_U32(1, h_peek32(server + 8));                 /* DISPLAY_RESTORED */
    vblank();
    s = at_line(50);
    CHECK_U32(plane(sbm, 0), s.bpl1);
    CHECK_U32(0x0333, s.colors[1]);

    CHECK(acquire(server, 0, NULL) != 0, "acquired twice");
    CHECK(release(game) != 0, "released twice");
}

/* The owner that wants the bare hardware. The kernel keeps its hands off
 * for as long as it is on top, and puts everything back afterwards. */
static void t_raw_owner_gets_the_hardware(void)
{
    uint32_t server = new_owner(), game = new_owner(), sbm;
    band sb = { 0, 100, 0, 0, BAND_HIRES, 0x0111 };
    unsigned cop, dma;
    shown s;

    setup();
    sbm = sb.bitmap = bm_alloc(640, 100, 2, server);
    acquire(server, 0, NULL);
    program(server, &sb, 1, 0);
    vblank();

    CHECK_U32(0, acquire(game, DISPLAY_RAW, NULL));
    cop = h_custom_writes(R_COP1LCH);
    dma = h_custom_writes(R_DMACON);
    sb.c1 = 0x0444;
    program(server, &sb, 1, 0);                         /* remembered, not shown */
    vblank(); vblank(); vblank();
    CHECK_U32(cop, h_custom_writes(R_COP1LCH));         /* untouched */
    CHECK_U32(dma, h_custom_writes(R_DMACON));

    release(game);
    CHECK(h_custom_writes(R_DMACON) > dma, "DMACON not restored");
    CHECK(h_custom(R_DMACON) & 0x0100, "bitplane DMA not back on");
    vblank();
    CHECK(h_custom_writes(R_COP1LCH) > cop, "copper list not restored");
    s = at_line(50);
    CHECK_U32(plane(sbm, 0), s.bpl1);
    CHECK_U32(0x0444, s.colors[1]);
}

/* --- the boot console ------------------------------------------------------ */

static uint32_t console_plane0;

static void console_setup(void)
{
    setup();
    CHECK_U32(0, kcall("kernel:_dcon_init", 0, NULL));
    vblank();
    console_plane0 = at_line(0).bpl1;
}

static void kputs(const char *s)
{
    uint32_t a[3]; a[0] = h_str(s); a[1] = (uint32_t)strlen(s); a[2] = 1;
    kcall("kernel:_dcon_write", 3, a);
}

/* Is glyph `ch` drawn at text cell (row, col) of whatever is on screen? Goes
 * through the copper: screen row -> raster line -> bitmap address. */
static int glyph_at(int row, int col, unsigned ch)
{
    uint32_t font = h_sym("kernel:_font8x8") + ch * 8u;
    shown top = at_line(0);
    uint32_t rows_in_bitmap = 32, base = console_plane0;
    const uint32_t step = 160;                          /* 2 planes of 80 bytes */
    uint32_t first_row = (top.bpl1 - base) / (step * 8u);
    uint32_t prow = (first_row + (uint32_t)row) % rows_in_bitmap;
    int y;

    for (y = 0; y < 8; y++)
        if (h_peek8(base + (prow * 8u + (uint32_t)y) * step + (uint32_t)col) != h_peek8(font + (uint32_t)y))
            return 0;
    return 1;
}

static void t_console_draws_text(void)
{
    console_setup();
    kputs("Hi \xE6");                                   /* ae: Latin-1 */
    kcall("kernel:_dcon_render", 0, NULL);

    CHECK(glyph_at(0, 0, 'H'), "no H at 0,0");
    CHECK(glyph_at(0, 1, 'i'), "no i at 0,1");
    CHECK(glyph_at(0, 3, 0xE6), "no ae at 0,3");
    CHECK(!glyph_at(0, 0, 'i'), "glyph_at says yes to anything");
    CHECK(glyph_at(5, 5, ' '), "rest of the screen is not blank");
}

/* Writers only touch the grid. Nothing reaches chip RAM until a render. */
static void t_console_is_lazy(void)
{
    console_setup();
    kputs("X");
    CHECK(!glyph_at(0, 0, 'X'), "drawn without a render");
    kcall("kernel:_dcon_render", 0, NULL);
    CHECK(glyph_at(0, 0, 'X'), "not drawn by a render");
}

/*
 * What drawing costs, because on this CPU it is easy to get badly wrong: the
 * first draw_row multiplied inside the pixel loop, and a 68000 multiplies
 * 32-bit values by calling a routine. A frame is about 141,800 cycles.
 * Measured when this was written: 2.24M for a full screen, 88k for one row.
 * A full redraw is rare - scrolling moves no memory - so the second number
 * is the one that is felt.
 */
static void t_rendering_is_affordable(void)
{
    h_result r;
    int i;

    console_setup();
    for (i = 0; i < 32; i++)
        kputs("the quick brown fox jumps over the lazy dog 0123456789 THE QUICK BROWN FOX\n");

    h_begin_call();
    h_set_cycle_budget(20000000);
    r = h_call(h_sym("kernel:_dcon_render"));
    CHECK_CALL(r);
    CHECK(r.cycles < 2600000, "a full screen redraw took %lu cycles - %lu frames",
          (unsigned long)r.cycles, (unsigned long)(r.cycles / 141800));

    kputs("x");                                         /* one row dirty */
    h_begin_call();
    r = h_call(h_sym("kernel:_dcon_render"));
    CHECK_CALL(r);
    CHECK(r.cycles < 110000, "redrawing one row took %lu cycles - a frame is 141,800",
          (unsigned long)r.cycles);
}

/* Forty lines into thirty-two rows. The early ones are gone, the late ones
 * are in order at the bottom - and the bitmap was scrolled by moving the
 * band's window onto it, not by moving memory. */
static void t_console_scrolls_by_moving_the_window(void)
{
    char line[16];
    uint32_t top_before;
    int i;

    console_setup();
    top_before = at_line(0).bpl1;
    for (i = 0; i < 40; i++) {
        line[0] = 'l'; line[1] = (char)('0' + i / 10); line[2] = (char)('0' + i % 10);
        line[3] = '\n'; line[4] = 0;
        kputs(line);
    }
    kcall("kernel:_dcon_render", 0, NULL);
    vblank();

    CHECK(at_line(0).bpl1 != top_before, "display still starts at bitmap row 0");
    CHECK(glyph_at(0, 1, '0') && glyph_at(0, 2, '9'), "top row should be l09");
    CHECK(glyph_at(30, 1, '3') && glyph_at(30, 2, '9'), "row 30 should be l39");
}

static void t_kprintf_reaches_the_screen(void)
{
    uint32_t a[2];

    console_setup();
    a[0] = 3; a[1] = h_str("boot ok\n");
    kcall("kernel:_kprintf", 2, a);
    kcall("kernel:_dcon_render", 0, NULL);

    CHECK(glyph_at(0, 0, 'b') && glyph_at(0, 5, 'o') && glyph_at(0, 6, 'k'), "kprintf text not on screen");
    kcall("kernel:_ser_flush", 0, NULL);
    CHECK_CONTAINS("boot ok", h_serial());              /* and still on serial */
}

/* The console is the bottom of the stack: it gives way, and it comes back. */
static void t_console_gives_way_and_returns(void)
{
    uint32_t game = new_owner();
    band gb = { 0, 200, 0, 0, 0, 0x0222 };

    console_setup();
    kputs("before");
    kcall("kernel:_dcon_render", 0, NULL);

    gb.bitmap = bm_alloc(320, 200, 1, game);
    acquire(game, 0, NULL);
    program(game, &gb, 1, 0);
    vblank();
    CHECK_U32(plane(gb.bitmap, 0), at_line(10).bpl1);

    kputs(" and during");                               /* printing goes on */
    kcall("kernel:_dcon_render", 0, NULL);

    release(game);
    vblank();
    CHECK_U32(console_plane0, at_line(0).bpl1);
    CHECK(glyph_at(0, 0, 'b') && glyph_at(0, 11, 'd'), "console lost what was printed meanwhile");
}

/*
 * The point of all of it: a machine with no serial cable. Keys typed on the
 * Amiga's keyboard reach a console task through con0, the command runs, and
 * the answer is drawn on the screen by the render task off the vertical
 * blank. Everything real: scheduler, CIA, keyboard handshake, copper.
 */
static int row_starting(const char *word)
{
    int row, i;

    for (row = 0; row < 32; row++) {
        for (i = 0; word[i]; i++)
            if (!glyph_at(row, i, (unsigned char)word[i]))
                break;
        if (!word[i])
            return row;
    }
    return -1;
}

static void t_type_a_command_see_the_answer(void)
{
    static const uint8_t mem_return[] = { 0x37, 0xB7, 0x12, 0x92, 0x37, 0xB7, 0x44, 0xC4 };
    h_result r;
    size_t i;

    console_setup();
    h_poke32(h_sym("kernel:_rom_panic"), h_sym("debugger_entry"));
    kcall("kernel:_input_init", 0, NULL);
    kcall("kernel:_cia_init", 0, NULL);
    kcall("kernel:_kbd_init", 0, NULL);
    CHECK_U32(0, kcall("kernel:_dcon_start", 0, NULL));
    CHECK_U32(0, kcall("kernel:_console_init", 0, NULL));
    h_vbl_every(20011);

    h_set_sp(0x2E0000);
    h_set_sr(0x2000);
    h_set_cycle_budget(3000000);
    r = h_run(h_sym("kernel:_sched_start"));
    CHECK(r.status == H_TIMEOUT, "scheduler stopped: %s", r.detail);
    CHECK(row_starting("amag> ") >= 0, "no prompt on the screen");

    for (i = 0; i < sizeof mem_return; i++)
        h_key(mem_return[i]);
    h_set_cycle_budget(12000000);
    r = h_resume();
    CHECK(r.status == H_TIMEOUT, "scheduler stopped: %s", r.detail);

    CHECK(row_starting("amag> mem") >= 0, "typed command not echoed on the screen");
    CHECK(row_starting("heap check: ok") >= 0, "the answer is not on the screen");
    /* ...and only there: an answer belongs to the console that asked. */
    kcall("kernel:_ser_flush", 0, NULL);
    CHECK(strstr(h_serial(), "heap check") == NULL,
          "a command typed at the keyboard answered on the serial line");
}

/*
 * A task that exits holding the display must not keep it. Nothing else will
 * ever release on its behalf, so without this a game that crashes out leaves
 * a dead owner on top of the stack for ever and its bitmap slots taken.
 * Given back at exit, not whenever the machine next goes idle.
 */
static void t_dead_owner_gives_the_display_back(void)
{
    uint8_t zero[0x80] = {0};
    uint32_t blk, avail, a[5], chip_before;
    h_result r;

    console_setup();
    CHECK_U32(0, kcall("kernel:_dcon_start", 0, NULL));
    h_vbl_every(20011);

    /* Chip RAM is sampled from inside: see task.exit_is_reaped. */
    avail = h_alloc(zero, sizeof zero);
    h_poke32(avail + 4, h_sym("kernel:_task_sleep"));  h_poke32(avail + 8, 10);
    h_poke32(avail + 12, 1);                            /* ALLOC_CHIP */
    h_poke32(avail + 16, h_sym("kernel:_mem_avail"));
    a[0] = h_str("avail"); a[1] = h_sym("body_sampler"); a[2] = avail; a[3] = 1024; a[4] = 1;
    kcall("kernel:_task_create", 5, a);
    { uint32_t pool = 1; chip_before = kcall("kernel:_mem_avail", 1, &pool); }

    blk = h_alloc(zero, sizeof zero);
    h_poke32(blk + 4, h_sym("kernel:_task_current"));
    h_poke32(blk + 16, h_sym("kernel:_bitmap_alloc"));
    h_poke32(blk + 24, h_sym("kernel:_display_acquire"));
    h_poke32(blk + 32, 0xFFFFFFFFu);
    a[0] = h_str("grabber"); a[1] = h_sym("body_grab_display"); a[2] = blk; a[3] = 2048; a[4] = 2;
    kcall("kernel:_task_create", 5, a);

    h_set_sp(0x2E0000);
    h_set_sr(0x2000);
    h_set_cycle_budget(4000000);
    r = h_run(h_sym("kernel:_sched_start"));
    CHECK(r.status == H_TIMEOUT, "scheduler stopped: %s", r.detail);

    CHECK_U32(1, h_peek32(blk));                        /* it ran, and is gone */
    CHECK(h_peek32(blk + 28) != 0, "test setup: no bitmap was allocated");
    CHECK_U32(0, h_peek32(blk + 32));                   /* and did take the display */

    CHECK_U32(console_plane0, at_line(0).bpl1);         /* the console is back */
    CHECK(h_peek32(avail) >= 2, "sampler ran %u times", h_peek32(avail));
    CHECK_U32(chip_before, h_peek32(avail + 20));       /* and the bitmap is freed */
}

/* A kernel log line that arrives while someone is typing at the screen
 * starts on a line of its own, and does not run on from their prompt. */
static void t_log_does_not_run_on_from_a_prompt(void)
{
    uint32_t a[3];

    console_setup();
    a[0] = kcall("kernel:_dev_find", 1, (a[1] = h_str("con0"), &a[1]));
    CHECK(a[0] != 0, "no con0");
    {
        /* what a console does: write a prompt through the device */
        uint32_t dev = a[0], ops = h_peek32(dev + 8), w[3];
        h_result r;
        w[0] = dev; w[1] = h_str("amag> me"); w[2] = 8;
        h_begin_call();
        h_push32(w[2]); h_push32(w[1]); h_push32(w[0]);
        r = h_call(h_peek32(ops + 4));
        CHECK_CALL(r);
    }
    a[0] = 3; a[1] = h_str("disk0: media changed\n");
    kcall("kernel:_kprintf", 2, a);
    kcall("kernel:_dcon_render", 0, NULL);

    CHECK(glyph_at(0, 0, 'a') && glyph_at(0, 7, 'e'), "the typed line was disturbed");
    CHECK(glyph_at(0, 8, ' '), "log text ran on from the prompt");
    CHECK(glyph_at(1, 0, 'd') && glyph_at(1, 4, '0'), "log line not on its own row");
}

/* ------------------------------------------------------------------------ */

static const test_case tests[] = {
    { "bitmap_in_chip_ram",     t_bitmaps_live_in_chip_ram,         NULL },
    { "bitmap_nonsense",        t_bitmap_nonsense_is_refused,       NULL },
    { "stale_handle",           t_stale_handle_stays_dead,          NULL },
    { "one_band",               t_one_band,                         NULL },
    { "two_bands",              t_two_bands_two_palettes,           NULL },
    { "modulo_skips_planes",    t_modulo_skips_the_other_planes,    NULL },
    { "colours_quantised",      t_colours_are_quantised_for_ocs,    NULL },
    { "band_below_line_255",    t_band_below_raster_line_255,       NULL },
    { "yoffset_wraps",          t_yoffset_wraps,                    NULL },
    { "bad_programs_refused",   t_bad_programs_are_refused,         NULL },
    { "swap_waits_for_vblank",  t_swap_waits_for_vblank,            NULL },
    { "ownership_stack",        t_ownership_is_a_stack,             NULL },
    { "raw_owner",              t_raw_owner_gets_the_hardware,      NULL },
    { "console_draws_text",     t_console_draws_text,               NULL },
    { "console_is_lazy",        t_console_is_lazy,                  NULL },
    { "rendering_affordable",   t_rendering_is_affordable,          NULL },
    { "console_scrolls",        t_console_scrolls_by_moving_the_window, NULL },
    { "kprintf_on_screen",      t_kprintf_reaches_the_screen,       NULL },
    { "console_gives_way",      t_console_gives_way_and_returns,    NULL },
    { "type_command_see_answer", t_type_a_command_see_the_answer,   NULL },
    { "dead_owner_gives_back",  t_dead_owner_gives_the_display_back, NULL },
    { "log_starts_a_fresh_line", t_log_does_not_run_on_from_a_prompt, NULL },
};

const test_suite kdisplay_suite = { "kdisp", tests, sizeof tests / sizeof tests[0] };
