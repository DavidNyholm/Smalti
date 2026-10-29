# Changelog

Semantic versioning; during 0.x, any version may change the API, and every change that does is listed here.

## Unreleased (in `beta`, for 0.2)

Theme files, the first part of 0.2: a theme is a file in JSON with comments, read by a loader of the library's own that reports every error with its line and column and never half-applies a theme, and it can restyle single widgets as well as the shared colours and sizes. [Themes](docs/Themes.md) describes the format. Live reload is still to come.

- New: `parse_theme()` and `load_theme()`, returning a `ThemeLoad` (the theme, and a `ThemeError` for each problem); `Theme` and `TextTuning` compare with `==`.
- New: widget styles. A theme file's `widgets` section sets colours, radii and a disabled opacity per widget, variant and state (hover, pressed, checked, focused, disabled); `Theme::widget_styles` holds them as `StyleRule` values. Every built-in widget now draws through its style, whose defaults are the 0.1 look: the gallery draws the same pixels.
- Changed: a disabled control dims every part of itself. A switch's outline, a radio button's centre, a slider's track and knob outline, and progress bars used to stay at full strength. (The Widgets page said disabled controls draw at 40% opacity; it has always been 45%.)
- Changed: a secondary button's outline is no longer tinted under the pointer or while pressed; the tint and the press shade are now part of the button's fill.
- New: timed redraws. `Widget::redraw_at(time)` and `redraw_after(delay)` draw a widget once at a later time, with the loop asleep until then; the porting interface's `BackendHost` gains `next_deadline_ns()` and `run_timers()` for it.
- New: live reload. `App::watch_theme(file)` applies a theme file and reapplies it 50 ms after each save, through the system's change notifications; `on_theme_error` receives a broken save's errors while the last good theme stays. The porting interface's `PlatformBackend` gains `watch_file` and `stop_watching_file`.
- New: `smalti-theme check <file>`, a command-line checker built from the same loader (option `SMALTI_BUILD_TOOLS`).
- New: `gallery <file.jsonc>` opens the gallery in a theme file's theme and restyles it on every save.
- Changed: the built-in themes are now the files `themes/dark.jsonc`, `black.jsonc` and `light.jsonc`, compiled into the library. They give exactly the 0.1 values, field for field, and the gallery draws the same pixels.
- Fixed (review round 6): a theme file must be a regular file (a FIFO could block the loader forever) and is read to its end; X11 ignores an `Xft.dpi` that is not a finite number from 24 to 960 (`inf` reached undefined integer conversions); window sizes from `WindowOptions` are converted safely whatever their value; the canvas clamps colours outside 0 to 1 instead of drawing wrong ones; a lone carriage return ends a line in a theme file.
- Changed: a button is at least as tall as its text, which a theme's font size can make taller than `size.control`.
- Build: `cmake/embed_fonts.cmake` became `cmake/embed_files.cmake`, which embeds the themes too.
- Fixed: the 0.1.1 release still called itself 0.1.0 in its CMake package, so `find_package(smalti 0.1.1)` refused it (`find_package(smalti 0.1)` was not affected).

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
