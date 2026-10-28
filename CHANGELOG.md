# Changelog

Semantic versioning; during 0.x, any version may change the API, and every change that does is listed here.

## Unreleased: 0.1.0 "First light"

The first version: the look, the motion, the basic controls, on Windows and Linux.

- **Platform:** Win32 (per-monitor DPI, a dark title bar, frames paced to the display, presenting during window moves and resizes), X11 (Xlib, Xft.dpi scaling), and a headless backend for tests.
- **Presentation:** a CPU path through GDI or XPutImage; on Windows, a GPU compositor (Direct3D 11, flip-model swap chain) that takes over once its device is ready, created on a one-shot background thread so the first frame never waits for it.
- **Rendering:** an exact-area anti-aliasing rasterizer, closed-form soft shadows with cached corners, dithered gradients, Inter embedded as the default font, a bounds-checked TrueType reader with GPOS kerning, a glyph cache with a memory budget.
- **Core:** `Ref<T>` handles with generation checks, deferred removal, single-thread enforcement, exceptions from callbacks rethrown from `run()`, damage tracking, motion driven by the frame clock, keyboard focus with Tab order.
- **Widgets:** Label (with wrapping), Button (four variants), CheckBox, Switch, RadioButton (grouped by parent), Slider, ProgressBar (with an indeterminate mode), Separator, Card; layout with Column, Row, Grid and Spacer.
- **Themes:** Dark, Black (for OLED screens) and Light, defined in code.
- **Examples:** `hello` and `gallery`.
