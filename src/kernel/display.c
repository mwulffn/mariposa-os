/*
 * display.c - the display hardware, and who owns it
 *
 * See display.h and docs/display_design.md. Three jobs: chip RAM bitmaps
 * behind handles; turning a list of bands into a copper list and getting it
 * in front of the copper safely; and the stack of owners.
 */
#include "display.h"
#include "amiga_hw.h"
#include "mem.h"
#include "cpu.h"
#include "task.h"

/* Custom register offsets, as the copper names them. */
#define R_DIWSTRT  0x08E
#define R_DIWSTOP  0x090
#define R_DDFSTRT  0x092
#define R_DDFSTOP  0x094
#define R_BPL1PTH  0x0E0
#define R_BPLCON0  0x100
#define R_BPLCON1  0x102
#define R_BPLCON2  0x104
#define R_BPL1MOD  0x108
#define R_BPL2MOD  0x10A
#define R_COLOR00  0x180


#define FIRST_LINE     0x2C             /* raster line of display line 0 */

/* ---------------------------------------------------------------- bitmaps --- */

#define MAX_BITMAPS 32

static struct {
    struct bitmap_info info;
    void              *owner;
    unsigned char     *memory;          /* one allocation, all planes */
    unsigned char      used;
    unsigned char      generation;      /* so a stale handle is not a valid one */
} bitmaps[MAX_BITMAPS];

/* handle = generation << 8 | slot + 1 */
static int slot_of(bitmap_t bm)
{
    unsigned long slot = (bm & 0xFF) - 1;

    if (bm == 0 || slot >= MAX_BITMAPS || !bitmaps[slot].used ||
        bitmaps[slot].generation != ((bm >> 8) & 0xFF))
        return -1;
    return (int)slot;
}

bitmap_t bitmap_alloc(unsigned long width, unsigned long height,
                      unsigned long depth, void *owner)
{
    unsigned long bpr, plane_size, i;
    unsigned char *mem;
    int slot;

    if (width == 0 || width > 2048 || (width & 15) || height == 0 ||
        height > 2048 || depth == 0 || depth > BITMAP_MAX_DEPTH)
        return 0;

    bpr = width / 8;
    plane_size = bpr * height;
    mem = mem_alloc_tagged(plane_size * depth, ALLOC_CHIP, owner);
    if (!mem)
        return 0;
    for (i = 0; i < plane_size * depth; i++)
        mem[i] = 0;

    CRITICAL_ENTER();
    for (slot = 0; slot < MAX_BITMAPS && bitmaps[slot].used; slot++)
        ;
    if (slot < MAX_BITMAPS) {
        bitmaps[slot].used = 1;
        bitmaps[slot].generation++;
        bitmaps[slot].owner  = owner;
        bitmaps[slot].memory = mem;
        bitmaps[slot].info.width  = (unsigned short)width;
        bitmaps[slot].info.height = (unsigned short)height;
        bitmaps[slot].info.depth  = (unsigned short)depth;
        bitmaps[slot].info.bytes_per_row = (unsigned short)bpr;
        for (i = 0; i < BITMAP_MAX_DEPTH; i++)
            bitmaps[slot].info.planes[i] = i < depth ? mem + i * plane_size : 0;
    }
    CRITICAL_EXIT();

    if (slot == MAX_BITMAPS) {
        mem_free(mem);
        return 0;
    }
    return ((bitmap_t)bitmaps[slot].generation << 8) | (unsigned long)(slot + 1);
}

int bitmap_free(bitmap_t bm)
{
    unsigned char *mem = 0;
    int slot;

    CRITICAL_ENTER();
    slot = slot_of(bm);
    if (slot >= 0) {
        mem = bitmaps[slot].memory;
        bitmaps[slot].used = 0;
    }
    CRITICAL_EXIT();

    /* A bitmap still on screen goes on being read by bitplane DMA after
     * this. That is reading freed memory, which shows garbage and harms
     * nothing; the owner's next program puts it right. */
    if (!mem)
        return -1;
    mem_free(mem);
    return 0;
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

/* ------------------------------------------------------------- the stack --- */

#define MAX_OWNERS 4

struct holder {
    void             *owner;
    unsigned long     flags;
    display_notify_fn notify;
    unsigned long     nbands;
    unsigned short    background;
    struct display_band bands[DISPLAY_MAX_BANDS];
};

static struct holder stack[MAX_OWNERS];
static int depth;                       /* stack[depth-1] is on top */

static struct holder *find(void *owner)
{
    int i;

    for (i = 0; i < depth; i++)
        if (stack[i].owner == owner)
            return &stack[i];
    return 0;
}

/* ----------------------------------------------------------- copper lists --- */

/*
 * Three buffers, because a list cannot be rewritten while the copper might
 * be running it. The copper restarts from COP1LC at the top of every frame;
 * COP1LC is written in the vertical blank handler, which may be just before
 * or just after that restart. So the list being replaced may be in use for
 * one more frame, and must be left alone for that long: front is on screen,
 * retiring was on screen until the last swap, and only back is ever written.
 */
#define COPPER_BYTES 4096

static unsigned short *cop_front, *cop_retiring, *cop_back;
static int swap_pending;
static unsigned short *cp;              /* where the builder is writing */
static int past_255;

static struct waitq vblank_waiters;

static void move(unsigned short reg, unsigned short value)
{
    *cp++ = reg;
    *cp++ = value;
}

/* Wait for the start of a raster line. The copper's vertical comparison is
 * eight bits, so lines past 255 are reached by waiting for the last
 * position of line 255 - after which the counter has wrapped and "line 5"
 * means 261. */
static void wait_line(unsigned int line)
{
    if (line > 255 && !past_255) {
        *cp++ = 0xFFDF;
        *cp++ = 0xFFFE;
        past_255 = 1;
    }
    *cp++ = (unsigned short)(((line & 0xFF) << 8) | 0x01);
    *cp++ = 0xFFFE;
}

static void set_pointers(const struct bitmap_info *bi, unsigned long row)
{
    unsigned int p;

    for (p = 0; p < bi->depth; p++) {
        unsigned long addr = (unsigned long)bi->planes[p] + row * bi->bytes_per_row;

        move((unsigned short)(R_BPL1PTH + p * 4), (unsigned short)(addr >> 16));
        move((unsigned short)(R_BPL1PTH + p * 4 + 2), (unsigned short)addr);
    }
}

static void build(const struct holder *h)
{
    unsigned long i, c;

    if (!cop_back)
        return;                                 /* display_init found no chip RAM */
    cp = cop_back;
    past_255 = 0;

    move(R_BPLCON0, BPLCON0_COLOR);             /* no planes until a band */
    move(R_BPLCON1, 0);
    move(R_BPLCON2, 0);
    move(R_DIWSTRT, 0x2C81);
    move(R_DIWSTOP, 0x2CC1);                    /* PAL: 256 lines */
    move(R_COLOR00, h ? h->background : 0);

    for (i = 0; h && i < h->nbands; i++) {
        const struct display_band *b = &h->bands[i];
        struct bitmap_info bi;
        unsigned int first = FIRST_LINE + b->y;
        unsigned short mode, shown, modulo;

        if (bitmap_info(b->bitmap, &bi) != 0)
            continue;                           /* freed since: show nothing */

        mode  = (unsigned short)(BPLCON0_COLOR | (bi.depth << 12));
        shown = 40;
        if (b->flags & BAND_HIRES) {
            mode |= BPLCON0_HIRES;
            shown = 80;
        }
        modulo = (unsigned short)(bi.bytes_per_row - shown);

        /* Set the band up in the gap above it, where nothing is drawn. */
        wait_line(first - DISPLAY_BAND_GAP);
        move(R_DDFSTRT, (b->flags & BAND_HIRES) ? 0x003C : 0x0038);
        move(R_DDFSTOP, (b->flags & BAND_HIRES) ? 0x00D4 : 0x00D0);
        move(R_BPL1MOD, modulo);
        move(R_BPL2MOD, modulo);
        for (c = 1; c < b->ncolors; c++)        /* colour 0 is the background's */
            move((unsigned short)(R_COLOR00 + c * 2), b->colors[c]);
        set_pointers(&bi, b->yoffset);

        wait_line(first);
        move(R_BPLCON0, mode);

        /* A window onto a circular bitmap: where it runs off the bottom,
         * carry on from the top. Few enough writes to fit before the line
         * starts being drawn. */
        if ((unsigned long)b->yoffset + b->height > bi.height) {
            wait_line(first + (bi.height - b->yoffset));
            set_pointers(&bi, 0);
        }

        wait_line(first + b->height);
        move(R_BPLCON0, BPLCON0_COLOR);
    }

    *cp++ = 0xFFFF;
    *cp++ = 0xFFFE;
    swap_pending = 1;
}

static void rebuild(void)
{
    if (depth && (stack[depth - 1].flags & DISPLAY_RAW))
        return;                                 /* not ours to touch */
    build(depth ? &stack[depth - 1] : 0);
}

void display_vblank(void)
{
    if (vblank_waiters.head)
        wake_all(&vblank_waiters);

    if (!swap_pending || (depth && (stack[depth - 1].flags & DISPLAY_RAW)))
        return;

    custom.cop1lc = (unsigned long)cop_back;
    {
        unsigned short *was_front = cop_front;
        cop_front    = cop_back;
        cop_back     = cop_retiring;
        cop_retiring = was_front;
    }
    swap_pending = 0;
}

void display_wait_vblank(void)
{
    task_wait(&vblank_waiters);
}

/* What a raw owner may have changed, and the kernel wants back. */
static void claim_hardware(void)
{
    custom.dmacon = DMAF_SETCLR | DMAF_MASTER | DMAF_RASTER | DMAF_COPPER;
}

/* ---------------------------------------------------------------- program --- */

static int acceptable(void *owner, const struct display_band *bands,
                      unsigned long n)
{
    unsigned long i, next_free = 0;

    if (n > DISPLAY_MAX_BANDS)
        return 0;
    for (i = 0; i < n; i++) {
        const struct display_band *b = &bands[i];
        struct bitmap_info bi;
        int slot;

        if (b->height == 0 || b->y < next_free ||
            (unsigned long)b->y + b->height > DISPLAY_LINES || b->ncolors > 32)
            return 0;
        next_free = (unsigned long)b->y + b->height + DISPLAY_BAND_GAP;

        slot = slot_of(b->bitmap);
        if (slot < 0 || bitmaps[slot].owner != owner)
            return 0;                           /* not yours to show */
        bi = bitmaps[slot].info;

        if (b->flags & BAND_HIRES) {
            if (bi.width < 640 || bi.depth > 4) /* OCS: 16 colours in hires */
                return 0;
        } else if (bi.width < 320 || bi.depth > 5) {
            return 0;
        }
        if (b->yoffset >= bi.height || b->height > bi.height)
            return 0;
    }
    return 1;
}

int display_set_program(void *owner, const struct display_band *bands,
                        unsigned long nbands, unsigned long background)
{
    struct holder *h;
    unsigned long i;
    int rc = -1;

    CRITICAL_ENTER();
    h = find(owner);
    if (h && acceptable(owner, bands, nbands)) {
        for (i = 0; i < nbands; i++)
            h->bands[i] = bands[i];
        h->nbands = nbands;
        h->background = (unsigned short)background;
        if (h == &stack[depth - 1])
            rebuild();
        rc = 0;
    }
    CRITICAL_EXIT();
    return rc;
}

/* -------------------------------------------------------------- ownership --- */

int display_acquire(void *owner, unsigned long flags, display_notify_fn notify)
{
    struct holder *below = 0;
    int rc = -1;

    CRITICAL_ENTER();
    if (owner && depth < MAX_OWNERS && !find(owner)) {
        if (depth)
            below = &stack[depth - 1];
        stack[depth].owner  = owner;
        stack[depth].flags  = flags;
        stack[depth].notify = notify;
        stack[depth].nbands = 0;
        stack[depth].background = 0;
        depth++;
        rebuild();                              /* blank until it says otherwise */
        rc = 0;
    }
    CRITICAL_EXIT();

    if (rc == 0 && below && below->notify)
        below->notify(below->owner, DISPLAY_LOST);
    return rc;
}

int display_release(void *owner)
{
    struct holder *h, *top = 0;
    int was_top = 0, was_raw = 0, rc = -1;

    CRITICAL_ENTER();
    h = find(owner);
    if (h) {
        was_top = (h == &stack[depth - 1]);
        was_raw = (h->flags & DISPLAY_RAW) != 0;
        for (; h < &stack[depth - 1]; h++)
            h[0] = h[1];
        depth--;
        if (was_top) {
            if (was_raw)
                claim_hardware();
            rebuild();
            if (depth)
                top = &stack[depth - 1];
        }
        rc = 0;
    }
    CRITICAL_EXIT();

    if (top && top->notify)
        top->notify(top->owner, DISPLAY_RESTORED);
    return rc;
}

void *display_owner(void)
{
    return depth ? stack[depth - 1].owner : 0;
}

void display_owner_gone(void *owner)
{
    int i;

    display_release(owner);             /* -1 if it held nothing: fine */

    for (i = 0; i < MAX_BITMAPS; i++) {
        bitmap_t bm = 0;

        CRITICAL_ENTER();
        if (bitmaps[i].used && bitmaps[i].owner == owner)
            bm = ((bitmap_t)bitmaps[i].generation << 8) | (unsigned long)(i + 1);
        CRITICAL_EXIT();
        if (bm)
            bitmap_free(bm);
    }
}

static void task_gone(struct task *t)
{
    display_owner_gone(t);
}

void display_init(void)
{
    int i;

    for (i = 0; i < MAX_BITMAPS; i++)
        bitmaps[i].used = 0;
    depth = 0;
    swap_pending = 0;
    vblank_waiters.head = vblank_waiters.tail = 0;

    cop_front    = mem_alloc(COPPER_BYTES, ALLOC_CHIP);
    cop_retiring = mem_alloc(COPPER_BYTES, ALLOC_CHIP);
    cop_back     = mem_alloc(COPPER_BYTES, ALLOC_CHIP);
    if (!cop_front || !cop_retiring || !cop_back)
        return;                                 /* no display; serial still works */

    build(0);                                   /* a blank screen, to start */
    claim_hardware();
    task_on_exit(task_gone);
}
