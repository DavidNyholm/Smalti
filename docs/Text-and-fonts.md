# Text and fonts

## Inter, embedded

smalti's default font is Inter (regular, medium and semibold), compiled into the library from `fonts/` at configure time, so a program needs no font files next to it and text looks the same on every system. Inter is under the SIL Open Font License 1.1; `fonts/SOURCE.md` records the release it came from and the SHA-256 of each file, so anyone can check them.

## The TrueType reader

smalti reads TrueType (`glyf`) fonts itself: the `cmap` formats 4 and 12, `glyf` and `loca` including composite glyphs, `hmtx`, `hhea`, `OS/2`, and pair kerning from `GPOS` (Inter has no legacy `kern` table). Every lookup of the `kern` features adds its adjustment, as the OpenType rules say. The `kern` features of all scripts are taken together, rather than the one for the text's script: the same for Inter, whose scripts share their kerning lookups, but a font whose scripts kern the same pair in different lookups would have that pair kerned twice. Every read goes through one bounds-checked reader, because a font file is input: a truncated or damaged font is refused, or loads with its damaged glyphs drawn as nothing. The tests run hundreds of truncated and corrupted copies of Inter through it under AddressSanitizer. CFF-flavoured `.otf` fonts are not read in v0.x.

## How text is drawn

- **One rasterizer for everything.** Glyph outlines go through the same exact-area anti-aliasing rasterizer as every shape, so text and widget edges look alike.
- **Grayscale anti-aliasing, never subpixel.** ClearType-style subpixel text assumes an RGB stripe; OLED panels use other layouts and show colour fringes with it. Grayscale has no fringes on any panel.
- **Quarter-pixel positioning** horizontally, so spacing stays even.
- **Adaptive blending:** light text on dark backgrounds blends in linear light and dark text on light backgrounds in sRGB, so both read at a similar weight. A slight contrast boost (0.1) sharpens edges, and the x-height is snapped to a whole pixel by stretching vertically by at most 8%. These are the theme's `text_tuning`.
- **A glyph cache** with a memory budget keeps each glyph at each size and quarter-pixel offset.

## Not in v0.x

Complex shaping (Arabic, the Indic scripts), right-to-left and mixed-direction text, colour emoji, ligatures, and fallback to a second font for missing characters. Latin, Greek and Cyrillic work.
