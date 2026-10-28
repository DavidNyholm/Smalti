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

Before the release, two rounds of independent review found 25 and then 21 defects (one blocker: on Windows, the GPU path could stop repainting). All are fixed, most with a regression test; [How it is checked](docs/How-it-is-checked.md) lists them. The fixes that show in the API:

- `MouseEvent::Kind::cancel`: a press that ends without a release, because the widget was hidden, disabled or removed, or the system took the mouse away.
- `WindowEvents::on_capture_lost` (for porting): the backend reports a lost mouse capture.
- `WindowOptions::min_size` is never larger than the requested `size`.
- `Grid` with no columns gets one stretching column; a child spanning several columns widens them to fit.
- `Callback` takes any callable, including one whose result the signature discards, and a callback may replace itself while it runs.
- `App::run()` cannot be called while it is already running.
- Adding a widget from `measure()`, `arrange()` or `paint()` is a contract violation.
- Every `App` call except `post()` checks that it is on the UI thread; `quit()` from a worker is a contract violation (post it instead).
- One mouse press at a time, ended only by the button that started it.
- Invalid UTF-8 of every kind (overlong, surrogates, past U+10FFFF) draws as U+FFFD.
- Glyphs larger than a million pixels are not drawn.
