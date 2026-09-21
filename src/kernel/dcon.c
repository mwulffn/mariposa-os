/*
 * dcon.c - the boot console: kernel text on the screen
 *
 * Retained and lazy. Writers change a grid of character cells in fast RAM
 * and mark rows dirty; they never touch chip RAM, so printing costs the same
 * from an interrupt handler as from a task. A task wakes on the vertical
 * blank and draws whatever changed. A burst of a thousand lines is a few
 * redraws, not a thousand - fast RAM spent to save cycles, which is the
 * trade this machine wants (docs/driver_design.md).
 *
 * Scrolling moves nothing. The bitmap is circular: a row of text falling off
 * the top is the same memory as the row arriving at the bottom, and what
 * changes is the band's yoffset - the copper's bitplane pointers, in the
 * end. One row is cleared and redrawn per scroll instead of thirty-two.
 *
 * Glyphs are drawn by the CPU, not the blitter. A byte-aligned 8x8 glyph is
 * eight byte writes per plane; setting the blitter up for one costs more
 * than that before it moves a bit. The loop that does it is in dcon_draw.s.
 */
#include "dcon.h"
#include "display.h"
#include "font8x8.h"
#include "chardev.h"
#include "input.h"
#include "task.h"
#include "cpu.h"
#include "kprintf.h"

#define GLYPH_H  8

static char          text[DCON_ROWS][DCON_COLS];
static unsigned char colour[DCON_ROWS][DCON_COLS];
static unsigned char dirty[DCON_ROWS];      /* by physical row */
static int top;                             /* physical row of logical row 0 */
static int cur_row, cur_col;                /* logical */
static int program_stale;                   /* top moved: yoffset to update */
static int typing;                          /* con0 wrote last, and left the
                                             * cursor mid-line: a prompt */

static bitmap_t screen;
static struct bitmap_info bi;
static int owner_token;                     /* its address is our identity */
#define OWNER ((void *)&owner_token)

static int phys(int row) { return (top + row) % DCON_ROWS; }

/* ---------------------------------------------------------------- writing --- */

static void clear_row(int prow)
{
    int c;

    for (c = 0; c < DCON_COLS; c++) {
        text[prow][c] = ' ';
        colour[prow][c] = DCON_NORMAL;
    }
    dirty[prow] = 1;
}

static void newline(void)
{
    dirty[phys(cur_row)] = 1;               /* the cursor is leaving */
    cur_col = 0;
    if (cur_row < DCON_ROWS - 1) {
        cur_row++;
    } else {
        /* Scroll: the top row becomes the bottom one. Nothing moves. */
        clear_row(top);
        top = (top + 1) % DCON_ROWS;
        program_stale = 1;
    }
    dirty[phys(cur_row)] = 1;
}

void dcon_write(const char *buf, unsigned long len, int col)
{
    unsigned long i;

    if (!screen)
        return;

    CRITICAL_ENTER();
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)buf[i];

        if (c == '\n') {
            newline();
        } else if (c == '\r') {
            cur_col = 0;
            dirty[phys(cur_row)] = 1;
        } else if (c == '\b') {
            if (cur_col)
                cur_col--;
            dirty[phys(cur_row)] = 1;
        } else if (c == '\t') {
            cur_col = (cur_col + 8) & ~7;
            if (cur_col >= DCON_COLS)
                newline();
        } else if (c >= ' ') {
            if (cur_col >= DCON_COLS)
                newline();
            text[phys(cur_row)][cur_col] = (char)c;
            colour[phys(cur_row)][cur_col] = (unsigned char)col;
            dirty[phys(cur_row)] = 1;
            cur_col++;
        }
    }
    CRITICAL_EXIT();
}

/* ---------------------------------------------------------------- drawing --- */

static void set_program(int yoffset)
{
    struct display_band band;
    int i;

    band.y = 0;
    band.height = DCON_ROWS * GLYPH_H;
    band.bitmap = screen;
    band.yoffset = (unsigned short)yoffset;
    band.flags = BAND_HIRES;
    band.ncolors = 4;
    band.reserved = 0;
    for (i = 0; i < 32; i++)
        band.colors[i] = 0;
    /* Workbench 1.3's four, because the font is from the same place. */
    band.colors[1] = 0x0FFF;                /* normal: white */
    band.colors[2] = 0x0002;                /* dim: black */
    band.colors[3] = 0x0F80;                /* bright: orange */
    display_set_program(OWNER, &band, 1, 0x005A);
}

/* dcon_draw.s. In assembly because it is the one loop here that matters:
 * the C version, even with every multiply taken out of it, cost about 2,700
 * cycles a cell - a second for a full screen. */
void dcon_draw_cells(unsigned char *p0, unsigned char *p1, const char *chars,
                     const unsigned char *colours, unsigned long bytes_per_row,
                     long cursor_col);

static void draw_row(int prow, const char *chars, const unsigned char *cols,
                     int cursor_col)
{
    unsigned char *p0 = bi.planes[0], *p1 = bi.planes[1];
    int y;

    /* Down to the row by adding: a 68000 multiplies 32-bit values by
     * calling a routine. */
    for (y = prow * GLYPH_H; y > 0; y--) {
        p0 += bi.bytes_per_row;
        p1 += bi.bytes_per_row;
    }
    dcon_draw_cells(p0, p1, chars, cols, bi.bytes_per_row, cursor_col);
}

void dcon_render(void)
{
    char chars[DCON_COLS];
    unsigned char cols[DCON_COLS];
    int prow, c, cursor, moved, yoffset = 0;

    if (!screen)
        return;

    for (prow = 0; prow < DCON_ROWS; prow++) {
        /* Take a copy of the row with writers held off, draw with them let
         * back in: drawing is the slow part and a writer may be a handler. */
        CRITICAL_ENTER();
        c = dirty[prow];
        dirty[prow] = 0;
        if (c) {
            /* Row pointers taken once: text[prow][c] inside the loop is a
             * multiply per character, and see draw_row about those. */
            const char *t = text[prow];
            const unsigned char *k = colour[prow];

            for (c = 0; c < DCON_COLS; c++) {
                chars[c] = *t++;
                cols[c]  = *k++;
            }
            c = 1;
        }
        cursor = (prow == phys(cur_row) && cur_col < DCON_COLS) ? cur_col : -1;
        CRITICAL_EXIT();

        if (c)
            draw_row(prow, chars, cols, cursor);
    }

    CRITICAL_ENTER();
    moved = program_stale;
    program_stale = 0;
    yoffset = top * GLYPH_H;
    CRITICAL_EXIT();
    if (moved)
        set_program(yoffset);
}

static void render_task(void *arg)
{
    (void)arg;
    for (;;) {
        display_wait_vblank();
        dcon_render();
    }
}

/* ------------------------------------------------------------------- con0 --- */

/* The local console as a character device: the keyboard in, the screen out. */

static long con0_read(struct device *dev, void *buf, unsigned long len)
{
    struct input_event ev;

    (void)dev;
    if (len == 0)
        return 0;
    for (;;) {
        input_read(&ev);
        if (ev.value != KEY_UP && ev.ch && ev.ch < 0x100) {
            *(unsigned char *)buf = (unsigned char)ev.ch;   /* Latin-1 */
            return 1;
        }
    }
}

static long con0_write(struct device *dev, const void *buf, unsigned long len)
{
    (void)dev;
    dcon_write(buf, len, DCON_NORMAL);
    typing = 1;
    return (long)len;
}

static unsigned long con0_rx_ready(struct device *dev)
{
    (void)dev;
    return input_pending();     /* an upper bound: not every event is a char */
}

static const struct chardev_ops con0_ops = { con0_read, con0_write, con0_rx_ready };
static struct device con0 = { "con0", DEV_CHAR, &con0_ops, 0, 0 };

/* ------------------------------------------------------------------- setup --- */

/* The kernel log arrives whenever it likes, including while somebody is
 * halfway through typing a command. It starts on a line of its own and does
 * not run on from their prompt. */
static void mirror(const char *buf, unsigned long len, int level)
{
    if (typing && cur_col != 0)
        dcon_write("\n", 1, DCON_NORMAL);
    typing = 0;
    dcon_write(buf, len, level <= KL_ERR ? DCON_BRIGHT : DCON_NORMAL);
}

int dcon_init(void)
{
    int r;

    screen = bitmap_alloc(640, DCON_ROWS * GLYPH_H, 2, OWNER);
    if (!screen || bitmap_info(screen, &bi) != 0 ||
        display_acquire(OWNER, 0, 0) != 0) {
        screen = 0;
        return -1;
    }

    top = cur_row = cur_col = 0;
    for (r = 0; r < DCON_ROWS; r++)
        clear_row(r);
    set_program(0);

    kprintf_sink = mirror;
    dev_register(&con0);
    return 0;
}

int dcon_start(void)
{
    if (!screen)
        return -1;
    /* Below the consoles: drawing is the expensive part, and whoever is
     * typing should not wait for the screen to finish catching up. */
    return task_create("screen", render_task, 0, 2048, TASK_PRIO_NORMAL) ? 0 : -1;
}
