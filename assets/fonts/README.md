# Fonts

## topaz-unicode-ks13.bdf

Source for the kernel's boot console font (`src/kernel/font8x8.c`, generated
by `tools/mkfont.py`). Only the Latin-1 range is used.

- **From:** Topaz Unicode by Screwtapello,
  <https://gitlab.com/Screwtapello/topaz-unicode>, `src/regular-glyphs.bdf`.
- **Terms:** the author states that bitmap fonts cannot practically be
  copyrighted and offers the repository under the ISC licence "if you would
  like to be safe".
- **Provenance, stated plainly:** this is not a redrawing. Its README says
  "all the original Topaz glyphs are present and unmodified" - they come from
  the Kickstart 1.3 ROM by way of Rob Hagemans' Hoard of Bitfonts - with new
  glyphs added outside Latin-1. The original is Commodore's. That bitmap
  typefaces are not copyrightable is well established in the United States
  and less settled elsewhere. Whether to ship it is a decision for the
  project's owner, and it is a cheap one to reverse: any 8-pixel-wide BDF
  works, and changing font is one run of `tools/mkfont.py`.
