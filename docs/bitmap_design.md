# Bitmap Standard

> **Status.** Implemented in `src/kernel/bitmap.c` and used by `display.c` and
> the boot console; pinned by `kbmp.*` and `kdisp.*`. The blitter driver, the
> chunky import call and the ILBM loader described here are not written; the
> standard is what they will be written to.

One in-memory representation for every bitmap in the system, fixed before
there is a second consumer to disagree with the first.

## Planar, because the chipset is

The custom chips display bitplanes. Converting a chunky screen to planar
costs a 68000 about a frame of CPU, so chunky is not an option for anything
shown. System bitmaps are planar, and the API is pitched so that callers
rarely touch a plane: fill, copy, draw text, and "put these chunky pixels
here" for import. Wasm applications will think in bytes per pixel; that
conversion lives behind a host call, once, not in every application.

## Rows are interleaved

```
row 0: plane 0 | plane 1 | ... | plane d-1
row 1: plane 0 | plane 1 | ... | plane d-1
...
```

Plane `p` starts at `base + p * bytes_per_row`, and consecutive rows of the
same plane are `row_step = depth * bytes_per_row` apart.

**Why:** a full-depth copy, clear or scroll of any rectangle is *one* blit
over `height * depth` rows instead of `depth` blits - one blitter setup and
one interrupt. It holds for sub-rectangles too, since every plane shares the
same horizontal layout, and scrolling a column inside a band is exactly what
a window manager does all day. The alternative, planes stored one after
another, makes each of those `depth` blits.

Nothing in the hardware minds:

- The copper's modulo registers take `row_step - bytes_fetched`, and the
  circular-bitmap scroll is unchanged. Dual playfield still works: odd and
  even planes have separate modulos.
- The blitter's line and fill modes take `row_step` as their modulo.
- **IFF ILBM's `BODY` is stored exactly this way** - per scanline, plane 0's
  row, then plane 1's, each padded to 16 bits - so loading one is a
  decompress straight into place with no reshuffling.
- On a 68030 and up the planes of a pixel row share a cache line. On a 68000
  there is no cache to care.

**What it costs, and what is done about each:**

- *The OCS blitter stops at 1024 rows a blit.* 256 lines of 4 planes is
  exactly 1024; 5-plane lores is 1280. The blit layer splits; callers never
  see it.
- *A single plane is no longer a contiguous image.* Hence two strides in the
  descriptor, not one: `bytes_per_row` is how much data a row holds,
  `row_step` is how far apart rows are. With both, one plane of any bitmap is
  itself a valid 1-plane bitmap - `bitmap_plane_view()` - so a plane can
  still serve as a mask for free. **Code steps rows by `row_step` and never
  by `bytes_per_row`**, and never computes either from the width.
- *Depth is part of the layout.* Adding a plane means reallocating. Rare.

## Masks

A mask is a separate 1-plane bitmap the size of its image. Cookie-cutting
with one is a blit per plane, as it would be in any layout.

A mask may instead be stored *replicated* - its row repeated once per plane
of the image it belongs to - which makes a masked blit a single blit again,
at `depth` times the chip RAM. That is an explicit trade for things drawn
constantly, never the default: chip RAM is the scarce kind.

## The descriptor

```
struct bitmap_info {
    width, height, depth
    format          BMFMT_PLANAR today
    flags           BITMAP_DISPLAYABLE
    bytes_per_row   data in one row of one plane; width / 8, word aligned
    row_step        distance between rows of the same plane
    planes[6]       where row 0 of each plane starts
}
```

- **`format` exists from day one** although it has one value. The target
  runs up to a 68060, those machines tend to have graphics cards, and a
  chunky framebuffer should be an addition, not a redesign - the same lesson
  as versioning `struct bootinfo`.
- **Width is a multiple of 16.** The blitter works in words. AGA's faster
  fetch modes will want 64; an explicit stride makes that a later change to
  the allocator and to nothing else.
- Bitmaps are handed out as **handles**, with a generation so that a freed
  one stays dead. `bitmap_info()` gives a native caller the addresses; a
  Wasm caller never sees them.

## Where a bitmap lives

`BITMAP_DISPLAYABLE` puts it in chip RAM, where the copper and the blitter
can reach it, and is required to show it or blit to or from it. Without the
flag it goes wherever there is room, fast RAM first. Decoded images, glyph
caches and off-screen storage belong there and are uploaded when shown: fast
RAM spent to save the scarce kind (`docs/driver_design.md`).

## Colour

A system colour is 24-bit, `0x00RRGGBB`, everywhere above the hardware.
`display.c` quantises to OCS's 12 bits when it builds a copper list; AGA or
a graphics card will use all of it. The first version of `struct
display_band` held `$0RGB` words - the register format, leaking upwards.

A palette is not part of a bitmap. It is bound to one where the bitmap is
shown - in a display band - so the same bitmap can appear in different
colours, which is how focus can be shown without redrawing anything.

## Geometry

Fixed once, so that no two subsystems disagree by a pixel:

- Origin top left, y down.
- A rectangle is `{x, y, w, h}`.
- **Half-open:** a pixel is inside if `x <= px < x + w`. Two rectangles that
  share an edge share no pixels, and a rectangle of width 0 is empty.
- Coordinates are signed 16-bit. A rectangle partly or wholly outside a
  bitmap is legal and is clipped, not refused: drawing half a window off the
  edge of the screen is normal, not an error.

## Files

IFF ILBM is the native interchange format: already planar, already
interleaved, compressed with ByteRun1 - a few dozen lines - and every Amiga
tool ever written can produce it. Formats that need inflate and a
chunky-to-planar pass are userspace's.
