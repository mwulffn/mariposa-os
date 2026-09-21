/*
 * dcon.h - the boot console: kernel text on the screen
 *
 * What a booting kernel shows before anything else owns the display, and
 * what comes back when everything else has let go of it. It is the bottom
 * of the display ownership stack and uses the same calls any other owner
 * would. See docs/display_design.md.
 */
#ifndef DCON_H
#define DCON_H

#define DCON_COLS  80
#define DCON_ROWS  32

/* colours, which are also the attributes: bit 0 is plane 0, bit 1 plane 1 */
#define DCON_NORMAL  1
#define DCON_DIM     2
#define DCON_BRIGHT  3

/* After mem_init() and display_init(). Takes the display, hooks kprintf, and
 * registers con0: a chardev that reads the keyboard and writes the screen.
 * Returns -1 if there was no chip RAM for a screen. */
int  dcon_init(void);

/* Start the task that draws. Until then nothing reaches the screen unless
 * dcon_render() is called by hand. */
int  dcon_start(void);

/* Any context. Only the text grid is touched - fast RAM, no drawing. */
void dcon_write(const char *buf, unsigned long len, int colour);

/* Draw what has changed. Task context, or the kernel before there are any. */
void dcon_render(void);

#endif /* DCON_H */
