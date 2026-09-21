# Display Design

> **Status.** The kernel side is implemented - `display.c`, `dcon.c`,
> `dcon_draw.s` - and pinned by `kdisp.*`. Verified under FS-UAE by looking at
> it (`tools/fsuae-screenshot.sh`): the boot console in Topaz on a hires
> screen, scrolling by the circular-bitmap trick, which means the copper's
> mid-band pointer reload holds up against real horizontal timing and not
> only in the tests' copper. Sprites and the blitter are not started. There
> is no userspace yet to acquire the display; ownership is exercised by
> tests.

## Scope: mechanism, not policy

**The kernel owns the display hardware and decides nothing about what is
shown.** Tiling, stacking, focus, what a window is, terminal emulation,
whether there is a GUI at all - userspace. A window manager is one possible
display owner; a game that wants a full-screen lores display and its own
copper effects is another, equally legitimate.

The kernel has to own the hardware for two reasons. There is one of it. And
DMA is dangerous: with no MMU, a wrong bitplane pointer or blit destination
writes anywhere in chip RAM, so whoever programs those pointers must be
trusted - which means the kernel, on behalf of callers it has checked.

AmigaOS was fast because it was a very thin layer over this hardware, and
paid for it in other ways. This is deliberately a little thicker: one owner
at a time, everything validated, even where that costs speed.

Userspace will be both Wasm and native code, with shared libraries - probably
Amiga hunk format with jump-table libraries, though that is not decided. So
the display calls are written to be wrappable: **handles, not pointers into
kernel state; every argument checked; no call assumes its caller is kernel
code.** The same functions can then sit behind a library jump table and a
Wasm host call without being rewritten.

## Bitmaps

`bitmap_alloc(width, height, depth, owner)` returns a handle to chip RAM,
tagged with its owner like any allocation. A handle carries a generation, so
one that has been freed does not come back to life when its slot is reused
for somebody else's bitmap. `bitmap_info` gives a native caller the plane
addresses to draw into; a Wasm caller will never see them.

## The display program

```
struct display_band { y, height, bitmap, yoffset, flags, ncolors, colors[32] }
display_set_program(owner, bands, nbands, background)
```

A description of what the hardware can vary **down** the screen, and only
down it: the copper works by scanline. A band is a run of scanlines showing
one bitmap, in one mode, with one palette. Side by side is not something the
chipset can do with two bitmaps, so it is not something a band can say - a
window manager that wants columns puts them in one bitmap.

- **Bands need a gap.** The copper manages about fifteen register writes
  before a line starts being drawn, and a band needs more than that: mode,
  fetch window, modulos, palette, a pointer pair per plane. The only place
  to do it without it showing is a line where nothing is displayed, so bands
  are separated by `DISPLAY_BAND_GAP` lines of background colour. It is also
  where a border would go.
- **`yoffset` wraps.** A band may be a window onto a circular bitmap: where
  it runs off the bottom it carries on from the top, by reloading the
  bitplane pointers mid-band - few enough writes to fit before the line.
  This is how text scrolls without moving a byte.
- **Lines past 255** are reached the way copper lists reach them: a wait for
  `$FFDF`, after which the 8-bit vertical counter has wrapped.
- **A program is validated whole and refused whole**: overlap, no gap, off
  the bottom, a bitmap too narrow for its mode, more planes than OCS has in
  that mode, a handle never issued - and a bitmap that is not the caller's.

### Getting a list in front of the copper

Three buffers. The copper restarts from `COP1LC` at the top of every frame,
and `COP1LC` is written in the vertical blank handler, which may run just
before or just after that restart. So the list being replaced may be in use
for one more frame and must be left alone that long: *front* is on screen,
*retiring* was until the last swap, and only *back* is ever written. `COP1LC`
moves in the vertical blank and nowhere else.

## Ownership

One owner at a time, and a stack of them.

- **Acquiring** takes the display from whoever has it. They are told
  (`DISPLAY_LOST`), keep their bitmaps, and may go on drawing and changing
  their program unseen.
- **Releasing** gives it back exactly as it now is (`DISPLAY_RESTORED`).
- **The kernel's boot console is the bottom of the stack**, so it is what
  appears when everyone else has gone.

So a game asks for the display, the display server is told and steps aside,
and when the game exits the server's screen comes back - without the two
knowing anything about each other.

**`DISPLAY_RAW`** is for the owner that wants the bare hardware. For as long
as it is on top the kernel does not touch the copper, the bitplane registers
or `DMACON`; on release it reclaims them and reinstates the owner below. That
is taking over the machine in a way the system can recover from, which the
original could not offer.

**An owner that dies gives everything back.** `display.c` registers a task
exit hook (`task_on_exit`): when a task exits, the display is released if it
held it and every bitmap it owned is freed - at exit, in the task's own
context, not whenever the machine next goes idle. A game that crashes out
returns the screen to whoever was underneath. `task.c` knows nothing about
the display; it knows there are hooks.

Acquiring is not negotiated: the current owner is told, not asked. A polite
game asks the display server through the server's own interface first; the
kernel does not take a position on manners.

## The boot console

What a booting kernel shows before anything else owns the display. It uses
the same calls any other owner would.

- **Retained and lazy.** Writers change a grid of character cells in fast RAM
  and mark rows dirty; they never touch chip RAM, so printing costs the same
  from an interrupt handler as from a task. A task wakes on the vertical
  blank and draws what changed. A burst of a thousand lines is a few
  redraws. Fast RAM spent to save cycles: `docs/driver_design.md`'s budget
  rule.
- **Scrolling moves nothing.** One circular bitmap; a scroll clears one row
  and changes `yoffset`.
- **Glyphs are drawn by the CPU, not the blitter.** A byte-aligned 8x8 glyph
  is eight byte writes a plane; setting the blitter up for one costs more
  than that before it moves a bit. The blitter's job, when it has a driver,
  is rectangles.
- **The inner loop is assembly**, and the reason is measured. The first C
  version multiplied inside the pixel loop - on a 68000 a 32-bit multiply is
  a subroutine call - and did not finish inside a test's cycle budget. With
  every multiply removed, vbcc's loop still cost about 2,700 cycles a cell.
  `dcon_draw.s` is about 530. A full 80x32 redraw is 2.24M cycles, a third
  of a second, and rare; one dirty row is 88k, a little over half a frame.
  `kdisp.rendering_affordable` holds both.
- 640x256, two bitplanes, four colours. `kprintf` is mirrored to it, errors
  in the bright colour.
- **`con0`** is a chardev that reads the keyboard and writes the screen, and
  a second console task runs on it. Everything a console prints - prompt,
  echo, and what its commands answer - goes to its own device and nowhere
  else: `kprintf` is the kernel log and appears on every console, and a
  command's answer is not the kernel log. The first version printed answers
  with `kprintf`, and a command typed on the serial line answered on the
  screen after somebody else's waiting prompt.
- A kernel log line can still arrive while someone is typing at the screen.
  It starts on a line of its own and does not run on from their prompt.

**The font** is Topaz, from `assets/fonts/topaz-unicode-ks13.bdf` by way of
`tools/mkfont.py`; see `assets/fonts/README.md` for where it came from and
what that means. Any 8-pixel-wide BDF works and changing font is one command.

## Open

- **PAL is assumed.** 256 lines; an NTSC machine has 200.
- **Sprites**, for a pointer. **The blitter**: a queue of rectangle copies
  and fills with a completion, checked against bitmaps the caller owns.
- **Userspace**: the loader, the library format, and how these calls are
  exposed through them.
