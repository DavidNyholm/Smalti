# Changelog

Semantic versioning; during 0.x, any version may change the API, and every change that does is listed here.

## 0.1.2 (2026-10-30)

Three fixes from review round 6. That round reviewed the theme work for 0.2, and these defects in it were older than that work, so 0.1 has them too; [How it is checked](docs/How-it-is-checked.md) lists them. No API changes.

- X11 ignores an `Xft.dpi` setting that is not a finite number from 24 to 960. A setting of `inf` was taken as an infinite scale, which reached integer conversions that are undefined for it: windows opened at 0 by 0 logical pixels.
- Window sizes from `WindowOptions` (and their minimum sizes) are converted to device pixels safely on every backend, whatever their value: NaN or an infinite or enormous size opens a window between 1 and 32768 pixels instead of reaching an undefined conversion.
- The canvas clamps every colour it draws to the range 0 to 1 (NaN to 0). A colour outside it, from a custom widget or from a theme whose hover colour is opaque (a button's hover tint is half again as strong), was packed into a wrong colour.
- The CMake package reports this version; 0.1.1's still said 0.1.0, so `find_package(smalti 0.1.1)` refused it.
- Tests: the Release build of the tests compiles without a warning again, and the test runner's allocation counter replaces the nothrow `operator new` too, which AddressSanitizer otherwise reports as a mismatch once a test path uses it.

## 0.1.1 (2026-10-29)

Fixes from review rounds 4 and 5, on the released 0.1.0, among them a memory-safety defect (drawing far outside the window could write outside the frame buffer); [How it is checked](docs/How-it-is-checked.md) lists them all. Two findings about grid spans that overlap across fit columns are left for 0.2. The changes that show in the API or on screen:

- `testing::Driver` rethrows an exception that a callback threw during any driver call, `idle()` included, as `App::run()` would; a test that expects one catches it there. `Driver::cursor()` is new: the cursor the window shows.
- A zero-width border (`stroke_rounded_rect` with width 0) is one device pixel at every scale, as documented; it was two at scale 2.
- Drawing far outside the window (beyond 2^24 device pixels) is clamped instead of overflowing.
- On X11, mouse buttons other than the main three are ignored, as on Windows.
- A group with two radio buttons created selected settles on one when one is clicked or selected.
- Space or Enter on a button shows the press.
- The repository now carries its code style: `.clang-format`, `ruff.toml` and `.editorconfig`; the whole tree was reformatted once, with no change to the compiled code.

## 0.1.0 "First light" (2026-10-29)

The first version: the look, the motion, the basic controls, on Windows and Linux.

- **Platform:** Win32 (per-monitor DPI, a dark title bar, frames paced to the display, presenting during window moves and resizes), X11 (Xlib, Xft.dpi scaling), and a headless backend for tests.
- **Presentation:** a CPU path through GDI or XPutImage; on Windows, a GPU compositor (Direct3D 11, flip-model swap chain) that takes over once its device is ready, created on a one-shot background thread so the first frame never waits for it.
- **Rendering:** an exact-area anti-aliasing rasterizer, closed-form soft shadows with cached corners, dithered gradients, Inter embedded as the default font, a bounds-checked TrueType reader with GPOS kerning, a glyph cache with a memory budget.
- **Core:** `Ref<T>` handles with generation checks, deferred removal, single-thread enforcement, exceptions from callbacks rethrown from `run()`, damage tracking, motion driven by the frame clock, keyboard focus with Tab order.
- **Widgets:** Label (with wrapping), Button (four variants), CheckBox, Switch, RadioButton (grouped by parent), Slider, ProgressBar (with an indeterminate mode), Separator, Card; layout with Column, Row, Grid and Spacer.
- **Themes:** Dark, Black (for OLED screens) and Light, defined in code.
- **Examples:** `hello`, `gallery`, and `settings` (the sample in the docs).

Before the release, three rounds of review by GPT-6-Astra found 25, 21 and then 9 defects (one blocker: on Windows, the GPU path could stop repainting). All are fixed, most with a regression test; [How it is checked](docs/How-it-is-checked.md) lists them. The fixes that show in the API:

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
