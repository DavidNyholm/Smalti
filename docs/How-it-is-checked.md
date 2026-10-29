# How it is checked

There is no continuous integration yet. Every check on this page runs locally, with the commands given, on Windows with MSVC 2022 and on Linux (Ubuntu 24.04 under WSL) with GCC 14. A version is tagged only after all of them pass.

## The tests

`smalti_tests` holds 146 tests and runs in a few seconds. It needs no test framework, and no screen: most tests drive real widgets through the headless backend, with simulated mouse and keyboard input, a fake clock that advances one frame at a time, and the drawn pixels read back.

- **Rendering:** the rasterizer's coverage is exact (rectangles with fractional edges, a circle's area, holes from opposite winding, clipping), soft shadows' fast path matches the direct integral to within one step in 255, dithered gradients average to the true ramp.
- **Fonts and text:** Inter's metrics and kerning. Truncated and randomly damaged copies of the font (about 300) must either be refused cleanly or still work. Fonts built byte by byte in `tests/font_builder.hpp` stand in for hostile ones: a composite glyph that multiplies itself, a character map that lies about its size, kerning split across lookups, a 'kern' feature repeated a thousand times, a glyph thousands of pixels wide. Invalid UTF-8 (cut short, interrupted, overlong, surrogates, past U+10FFFF) decodes to the replacement character.
- **Lifetimes:** dead `Ref`s are reported and never dereferenced, slot reuse does not revive old `Ref`s, widgets remove themselves from their own callbacks, calls from another thread are refused, 2,000 rounds of building and removing controls and 300 windows opened and closed return to the starting widget count.
- **Layout, widgets and motion:** stacks, grids, every control by mouse and keyboard, focus order, animations that settle and then request no more frames.
- **Theme files** (in `beta`): every value form, and every error with its line and column (`tests/test_theme.cpp`); the reader's syntax errors and its limits (`tests/test_jsonc.cpp`); the built-in theme files against the 0.1 themes, field for field; and 3,000 randomly damaged copies of the built-in files, each of which must give either a theme inside the documented ranges, or errors that point inside the file and the base theme untouched. Widget styles (`tests/test_styles.cpp`): every default against the 0.1 drawing code's formulas, every form a `widgets` section can take and every error in it, a variant's rule winning over the widget's, and widgets drawing a theme's colours.
- **Memory:** an `operator new` counter, which counts calls and bytes, shows that an animated frame allocates nothing once warm and that hostile fonts cannot make the reader allocate much. It replaces every form of `operator new` that its `delete` frees, the nothrow one included: under AddressSanitizer a form left out comes from the sanitizer, which reports our `free()` of its memory as a mismatch.
- **Review regressions:** `tests/test_review.cpp` (round 1: one test for each of the 17 defects that can be reproduced without a screen, and three for related cases) `tests/test_review2.cpp` (round 2: 16 tests) and `tests/test_review3.cpp` (round 3: 7 tests) `tests/test_review4.cpp` (round 4: 12 tests) `tests/test_review5.cpp` (round 5: 5 tests) and `tests/test_review6.cpp` (round 6: 10 tests). See below.
- **Timed redraws and live reload** (in `beta`): a timed redraw costs one frame each time on the fake clock (`tests/test_timing.cpp`); live reload runs against real files and the system's own change reports, which the headless backend reads without blocking (`tests/test_reload.cpp`): the pause after a save, a broken save keeping the last good theme, an editor saving by renaming a new file over the old, a burst of saves reloading once, other files ignored. Those seven ran 20 times on Windows and 20 times on Linux, also under the sanitizers, without a failure.

```bash
cmake --preset windows
cmake --build --preset windows
build/windows/Debug/smalti_tests
```

On Linux, the `linux` preset and `./build/linux/smalti_tests`. A name fragment as the first argument runs only the matching tests: `smalti_tests review_`.

## Warnings

Both compilers build with strict warnings (`/W4 /permissive-` on MSVC, `-Wall -Wextra -Wpedantic` on GCC), in Debug and Release, without a single warning.

## Sanitizers

The `linux-asan` preset builds everything with AddressSanitizer, UndefinedBehaviorSanitizer (with its check for floating-point values too large for the integer they are converted to, which is not on by default) and LeakSanitizer. Undefined behaviour stops the run rather than printing a note. The whole suite passes clean. Several of the review regressions only fail loudly under it: a read past the end of a buffer or a use after free can go unnoticed in a normal build.

```bash
cmake --preset linux-asan
cmake --build --preset linux-asan
./build/linux-asan/smalti_tests
```

## Injected bugs

A test suite that passes proves little until it is seen to fail. `tools/mutate.py` puts one deliberate bug at a time into the sources, rebuilds and runs the suite, and reports whether any test failed. The bugs are the plausible ones: a stale `Ref` resolving to whatever reuses its slot, removed widgets never destroyed, damage never recorded, Shift+Tab moving forwards, the glyph cache never evicting, a shadow tile read one pixel off, kerning stopping after the first lookup, and so on, plus thirty-four that each undo one of the review fixes below, twenty-two in the theme file reader, the loader and the widget styles, twelve that undo round 6's fixes, and six in timed redraws and live reload.

Result on the current sources, MSVC Debug: **106 of 106 caught**, by a failing test or a crashing test binary, none by a time-out.

Getting there took some fixing of the tests themselves. The first run after round 1 caught 37 of 42, and every miss involved an older test or an injected bug, not a review fix:

- Shift+Tab moving forwards went unseen because the test had only two focusable widgets, where one step back and one step forward land on the same one.
- One injected bug removed a clamp that a second clamp makes redundant, so it changed nothing. It now removes the split of an edge where it crosses the clip's border, and the test's edge crosses that border halfway down a pixel row, where the split matters (before, it crossed exactly on a row boundary, where it does not).
- A UTF-8 lead byte followed by a plain letter was never tested.
- Removed widgets that are never destroyed made the churn test grind through all 2,000 rounds until it timed out; it now stops at the first leaked round.
- One injected bug itself was faulty: on MSVC, `v = {}` keeps a vector's memory, so it changed nothing.

After round 2, one injected bug (posted work never running) made a new test hang instead of fail: it kept an animation running while it waited. It now asks for frames only as its job progresses, so without posted work it ends at once and fails.

After round 4, the injected bug that ignores the share a span gives back to stretch columns went unseen: with the new solver it only slows convergence, in every case the tests had. It changes the result for a span wider than the whole grid over its only stretch column, which now has a test.

After round 5, one injected bug (reversed slider bounds reaching `std::clamp`) timed out instead of crashing: MSVC's debug check stopped the test binary in a way that waited instead of exiting. The test runner now has MSVC report such checks on stderr and exit at once.

The first run with the theme files caught 13 of their 14. The nesting limit had been tested with arrays only, and objects have a check of their own; a test now nests objects too. One other bug was caught only by a crash: a test read the third error of a list the bug had shortened to two. It now reads the list with `at()`, so such a bug fails the test instead.

With timed redraws and live reload, the first run caught 102 of 104. One test asked for an overflowing delay while the fake clock stood at 0, where adding to it cannot overflow; it now moves the clock first. Another saved a file in place to show that a stopped watch reports nothing, but Windows reports such a save only as its cache is written out, so the test saw nothing either way; it now saves by renaming a new file into place, which both systems report at once, and watches for longer.

```bash
python tools/mutate.py --preset windows
python tools/mutate.py --preset windows --only kerning
```

The second form runs only the bugs whose description contains the given text. A time-out counts as caught, since a suite that no longer finishes is a failure anyone sees, but it is reported separately.

## Review

Before its release, v0.1 had three rounds of code review, the released 0.1.0 a fourth and a fifth before 0.1.1, and the theme work in `beta` a sixth, three of whose findings 0.1.2 also fixes, by a second model, **GPT-6-Astra** (model id `gpt-6-astra`, at its highest reasoning effort) through the Codex CLI: read-only passes over the whole source, asked to act as a hostile senior engineer and to state each finding as a concrete failure scenario. Claude checked every finding against the code before fixing it; all of them turned out to be real. Where a defect could be reproduced without a screen, a regression test was written first and seen to fail before the fix.

### Round 1

It reported 25 defects: one blocker, twelve major, twelve minor. All 25 were fixed. Round 2 judged 18 of those fixes complete and 7 incomplete; the incomplete ones are finished in round 2 below.

| # | Defect | Fix | Checked by |
|---|---|---|---|
| 1 | **Blocker.** Windows, GPU path: a frame with nothing to draw used up the swap chain's "ready" signal without presenting, and the window stopped repainting for good. | The permission to present is kept until a frame is actually presented. | By hand: `hello` clicked after the GPU took over. Before the fix the label froze; after it, three clicks showed "Clicked 3 times.", idle CPU stayed at 0.0% and the animating gallery at 6.2% of one core. |
| 2 | Two shadow blurs that round to the same cache key needed corner tiles of different sizes: a read past the end of the cached tile. | Blur and radius are snapped before use, so a key always means one size; the size is checked as well. | Test, under ASan |
| 3 | A `Grid` with an empty column list read past the end of it. | It gets one stretching column. | Test, under UBSan |
| 4 | A callback that replaced itself (`on_click` inside the click handler) destroyed the running lambda and its captures. | The running callable is kept alive until it returns. | Test, under ASan |
| 5 | A callback that removed its widget and then ran a nested event loop (a message box) had the widget destroyed under it. | Removals wait for the outermost event; `run()` cannot be re-entered. | Test, under ASan |
| 6 | A widget invalidating another while painting could change the list being drawn, and its damage was dropped. | Frames paint from a batch of their own; new damage goes to the next frame. | Test |
| 7 | A window whose construction failed stayed registered with the backend (and on Windows, attached to its handle). | Registration comes last; a failed constructor releases what it made. | By reading; failures cannot be injected into window construction yet. |
| 8 | A tiny hostile font whose composite glyphs each repeat the level below could expand to billions of shapes. | One outline may visit at most 256 parts and read at most 131,072 points. | Test with a hand-built font |
| 9 | Children created inside a composite widget's constructor never learned their window: no hover, animation or focus. | Adding a widget attaches its whole subtree. | Test |
| 10 | A focused control that was hidden kept focus and still reacted to Space. | Hiding or disabling drops focus; keys only reach visible, enabled widgets. | Test |
| 11 | Disabling a control while it was pressed did not stop the press: it still toggled on release. | The press is cancelled (`MouseEvent::Kind::cancel`) on hide, disable, removal and lost capture. | Test |
| 12 | With 63 or more GPU windows waiting to draw, the Windows wait call failed and the loop spun. | Windows beyond the wait limit are polled at their refresh rate; a failed wait stops the loop with an error. | By reading |
| 13 | A `Slider` with min above max called `std::clamp` with reversed bounds (undefined behaviour). | Bounds are put in order first. | Test |
| 14 | A character map claiming 2^31 groups caused a signed overflow. | The search is unsigned, and every read is bounds-checked. | Test, under UBSan |
| 15 | Work posted from another thread waited while a window was being dragged. | Wakes are messages to a message-only window, so the system's own loops run them; the drag timer draws every window's due frame. | By reading; not yet by dragging a window while work is posted. |
| 16 | A hidden selected radio button took its whole group out of the Tab order. | Only a visible, enabled selection counts. | Test |
| 17 | A child spanning two fit columns contributed no width. | A spanning child widens the columns it covers. | Test |
| 18 | The slider knob's shadow reached past the area repainted for it, leaving traces. | The slider reports how far it paints outside its bounds. | By reading the geometry |
| 19 | An empty window never drew its first frame. | The first frame is requested once the native window exists. | Test |
| 20 | X11 accepted 16- and 30-bit displays and showed garbage. | The visual and the image format are checked, with a clear error. | Run on WSLg (24-bit); not yet on a 16-bit server. |
| 21 | Glyphs with no pixels (spaces) cost nothing against the cache budget, so the cache could grow without limit. | Each entry's bookkeeping counts against the budget. | Test |
| 22 | `Callback<void()>` accepted a lambda returning a value, then failed to compile. | Results are discarded; calls go through `std::invoke`. | Test |
| 23 | After 2^31 reuses of one slot, its generation number wrapped and an ancient `Ref` could resolve again. | The slot is retired before that. | By reading |
| 24 | Kerning stopped at the first lookup that applied, dropping later ones. | Each lookup contributes. | Test with a hand-built font |
| 25 | A failed GPU surface creation leaked a handle. | The handle is owned from the moment it is acquired. | By reading |

The review also confirmed, among other things, that ordinary self-removal and window closing were already safe, that a stale `Ref` never touches freed memory, and that exceptions never unwind through the operating system.

### Round 2

The second round reviewed the round-1 fixes and the rest of the code again. Round 3 judged 12 of these 21 fixes complete and 8 incomplete (the 21st, the injected bug, was already fixed); the incomplete ones are finished in round 3. It reported 21 defects (twelve major, nine minor): gaps in seven round-1 fixes, new defects some of those fixes introduced, and older ones round 1 missed. One (an injected bug that no longer applied) had been fixed already; the other 20 are fixed now, 16 of them with a test in `tests/test_review2.cpp` that failed first, two of those under ASan.

| # | Defect | Fix | Checked by |
|---|---|---|---|
| 1 | A setter called outside any event (`set_enabled(false)` during a press) sent a cancel whose handler could run a nested loop and free the widget while the setter still used it. | Setters that call into widgets count as event dispatch for their whole length. | Test, under ASan |
| 2 | A `paint()` that ran a nested loop could get a frame of its own window drawn inside the current one, sharing its damage list. | A window never draws a frame inside a frame; the nested one is drawn next. | Test |
| 3 | A widget added from `measure()` or `paint()` could reallocate the child list that layout or painting was walking. | That is a contract violation now, and layout no longer holds references into the list. | Test |
| 4 | Changing a wrapped `Label`'s text while painting left its cut lines pointing into the freed text. | The lines are dropped with the text and cut again at the next layout. | Test, under ASan |
| 5 | A failed frame buffer resize (out of memory) left the core drawing into freed pixels, on Windows and X11. | The new buffer is made before the old one goes; if that fails, the core is told it has none, and Windows stops drawing until the error is rethrown. | By reading; the failure cannot be injected yet. |
| 6 | Posted work that keeps posting its next piece kept the loop from ever drawing or reading input. | Each wake runs only what was posted before it; Windows handles at most 64 messages per turn. | Test |
| 7 | On Windows, a wake whose message could not be posted (a full queue) left the work queued until some later post. | The loop, and the drag timer, run the work themselves when that happens. | By reading |
| 8 | A press whose own handler disabled its widget (a slider's `on_change`) still captured the mouse, and the drag resumed when it was re-enabled. | The press is cancelled at once. | Test |
| 9 | On X11, minimising a window during a press never ended the press. | Unmapping the window cancels it. | By reading |
| 10 | A 'kern' feature record repeated thousands of times, all pointing at one feature table, made the reader store billions of lookup references. | Each feature table is read once into a set of lookups, with limits on references and subtables. | Test (bytes allocated) |
| 11 | A glyph thousands of pixels wide (a tiny units-per-em and huge coordinates) was rasterized whatever it cost: gigabytes before the cache budget applied. | Glyphs over a million pixels are not drawn. | Test (bytes allocated) |
| 12 | A `paint()` that threw lost the damage of that frame, leaving stale pixels after the app recovered. | A frame cut short redraws everything next time. | Test |
| 13 | `focus()` accepted a widget inside a hidden container. | It checks visibility and enabled state all the way up. | Test |
| 14 | The round-1 span fix gave a fit column all of a spanning header's width, squeezing a stretch column beside it to nothing. | A span over a stretch column leaves the width to that column. | Test |
| 15 | Kerning summed across lookups was cached in 16 bits, so the second answer differed from the first. | The sum is clamped to 16 bits before it is returned or cached. | Test |
| 16 | Pressing and releasing another mouse button during a press ended the press, and the click was lost. | One press at a time, ended only by its own button. | Test |
| 17 | A card's shadow inside a row was cut off when a repaint touched the shadow but not the row. | A parent outside the repaint area no longer hides its children. | Test (pixels) |
| 18 | Overlong UTF-8, surrogates and values past U+10FFFF decoded as characters. | They decode to U+FFFD. | Test |
| 19 | A headless-only install still asked its users for X11. | The installed package names only what was built. | Generated configs checked for both builds. |
| 21 | The README said every call checks the UI thread; `App`'s own methods did not. | They do now, except `App::post`. | Test |

After both rounds, `hello` on the GPU path still shows "Clicked 3 times." after three clicks and idles at 0.0% CPU, and the animating gallery measured 6.4 to 7.7% of one core over five 10-second runs, against 6.7 to 8.6% for the round-1 build, alternating on the same machine: no difference that this machine's noise lets through.

### Round 3

The third round concentrated on the round-2 changes. It reported 9 defects (five major, four minor), all fixed; `tests/test_review3.cpp` has a test for each of the six that can be reproduced without a screen, plus one for a second case of the grid defect, and each failed first.

| # | Defect | Fix | Checked by |
|---|---|---|---|
| 1 | On Windows, a backend failure recorded before `run()` (a window whose construction failed inside a message handler) made the loop skip frames forever without ever ending. | `run()` rethrows a pending failure before it starts. | By reading |
| 2 | `RadioButton::select()` called from code let its callback free the radio while `select()` was still running. | It counts as event dispatch, like the setters. | Test |
| 3 | On Windows, posted messages come out of the queue before mouse and keyboard input, so a job that kept posting kept input waiting, though frames now ran. | Each turn handles input first, then the rest. | By reading |
| 4 | On Windows, a failed wake post set a flag that a loop already going to sleep would not see. | The failure also signals an event that every wait includes. | By reading |
| 5 | A press handler that disabled and re-enabled its widget still left the press in force. | The press belongs to the widget while its handler runs, so the disable ends it. | Test |
| 6 | The round-2 grid rule gave a span over a zero-weight stretch column no width at all, and short-changed spans over stretch columns. | A span's shortfall is divided by the part of the growth it keeps once the width is shared out. | Two tests |
| 7 | A widget could be told it gained focus after the old widget's hook had already hidden it. | It is told only if it still has focus. | Test |
| 8 | A glyph with almost no area but coordinates beyond an `int` passed the size check and overflowed in the rasterizer. | Coordinates outside the rasterizer's range are refused. | Test, under UBSan's float-to-integer check |
| 9 | `run()` could return success after a callback failed during its startup cleanup. | It reports that failure. | Test |

After round 3, `hello` on the GPU path still shows "Clicked 3 times." and idles at 0.0% CPU, and the animating gallery measured a median of 7.7% of one core over five 10-second runs.

More rounds would likely find more: each round so far has, partly in the previous round's fixes. Version 0.1.0 is released after round 3 with every reported defect fixed, and the next version starts with another round.

### Round 4, on the released 0.1.0

The fourth round looked first at the round-3 changes, then took a fresh pass over the parts the earlier rounds had spent little time on: the public headers as an API, the drawing arithmetic at extreme sizes, the caches, the test harness itself and the CMake files. It reported 17 defects (seven major, ten minor), all fixed; `tests/test_review4.cpp` has a test for each of the eleven that can be reproduced without a screen, and each failed first, two of them only under UBSan, plus one for the grid case the injected bugs showed was missing.

| # | Defect | Fix | Checked by |
|---|---|---|---|
| 1 | A press handler that pumped the release in a nested loop and then declined the press let the press bubble on to a parent, which captured the mouse after the button was up. | Each press carries a serial; once nested dispatch has ended it, it goes no further. | Test |
| 2 | Grid spans in several rows were each measured against the same stale widths (260, 0, 0 where 100, 50, 50 fits), and four rounds were too few for spans that interact. | Every span sees what the earlier ones took, and the solver runs until all fit. | Two tests |
| 3 | On Windows, posted work that kept posting still starved input inside a native dialog, whose own loop hands out posted messages first. | A wake from the UI thread while input or painting waits is a timer, which every loop delivers after input. | By reading |
| 4 | On Windows, the round-3 lost-wake event was waited on only by smalti's own loop, not by a native dialog's. | A failed post is retried from a thread-pool timer until it succeeds. | By reading |
| 5 | `fill_rect` at x = 2^31 overflowed the rectangle arithmetic and wrote outside the frame buffer. | Floating-point edges become integer rectangles through one function that clamps them; text far off the canvas stops. | Test, under UBSan |
| 6 | A polyline with a height of 1e-37 px turned its slope into NaN, used as an index. | Edges flatter than a millionth of a pixel are skipped. | Test, under UBSan |
| 7 | The shadow corner cache kept every blur it had seen: hundreds of megabytes for an animated blur. | At most 1 MB, least recently used out first; very large tiles are not kept. | Test |
| 8 | On Windows, an exception while creating the GPU device ended the process. | It is caught; the app stays on the CPU path. | By reading |
| 9 | On Windows, failing to start the GPU thread leaked the loop's handles. | The CPU path is kept instead. | By reading |
| 10 | The test driver dropped exceptions from callbacks, so a test could pass while one failed. | Every driver call rethrows them. | Test |
| 11 | A project building smalti with `SMALTI_SANITIZE` could not link it. | The library passes the sanitizer link flags on. | A consumer project, failing without the fix |
| 12 | A widget could be told it gained the hover after the old widget's hook had hidden it. | It is told only if it still has the hover, as for focus. | Test |
| 13 | A zero-width border was two device pixels at scale 2, though documented as one. | One device pixel at any scale. | Test |
| 14 | The glyph size limit estimated from fractional bounds and could let through twice its pixels. | It counts the whole pixels the rasterizer allocates. | By reading |
| 15 | On X11, extra mouse buttons counted as the middle button and could end a middle-button press. | They are ignored, as on Windows. | By reading |
| 16 | Two radio buttons created selected in one group stayed so through a click. | Selecting one settles the group. | Test |
| 17 | Activating a button with Space showed no press. | The press shows, then fades. | Test |

Also checked after round 4: the X11 backend refuses a 16-bit display with its clear message and runs on a 24-bit one (both on Xvfb), and with the round-4 changes `hello` still shows "Clicked 3 times." on the GPU path, idles at 0.0% CPU, and the gallery measured a median of 5.9% of one core.

### Round 5, before 0.1.1

The fifth round concentrated on the round-4 changes. It judged 10 of those 17 fixes complete and 7 incomplete, and reported 11 defects (five major, six minor). Nine are fixed; `tests/test_review5.cpp` has a test for each of the five that can be reproduced without a screen, and each failed first, one only under UBSan.

| # | Defect | Fix | Checked by |
|---|---|---|---|
| 1 | On Windows, a wake retry running while the backend shut down could arm its timer again after it was disarmed, and run on freed memory. | Arming and shutting down are serialised under one lock. | By reading |
| 2 | A circle with a radius of 1e20 overflowed the curve's segment count before it was clamped. | The count is computed in double and clamped before it becomes an int. | Test, under UBSan |
| 3 | A focus hook that pumped the release of the press that focused it left a ghost press behind. | The press is under way from its first hook; such a release ends it. | Test |
| 4 | On Windows, a worker thread posting continuously could still keep a native dialog's input waiting, through its own wake messages. | A wake message that arrives while input or painting waits defers to a timer, and other threads' wakes are held back until then. | By reading |
| 5 | On Windows, a failed `SetTimer` left posted work waiting for some other wake. | It falls back to a posted message. | By reading |
| 6, 7 | Grid spans that overlap across fit columns can be given a poor split, overflowing the grid or leaving a span short, though a split that fits exists. | **Not fixed in 0.1.1.** It needs the span constraints solved together, which v0.2 does. | Open |
| 8 | On Windows, running out of memory while starting the GPU thread still leaked the loop's handles. | Any failure to start it keeps the CPU path. | By reading |
| 9 | `testing::Driver::idle()` still dropped exceptions from cleanup. | It rethrows them like every other driver call. | Test |
| 10 | Text placed beyond the canvas's working range stopped drawing the rest of its line and reported the wrong end. | Such glyphs are skipped, but the pen still advances. | Test |
| 11 | A widget's own hover hook that hid it could leave its cursor showing. | The cursor is set only for a widget that still has the hover. | Test |

After round 5, `hello` on the GPU path still shows "Clicked 3 times." and idles at 0.0% CPU, the gallery measured a median of 5.7% of one core, and the X11 checks on Xvfb and WSLg pass as before.

### Round 6, the theme work in beta

The sixth round reviewed theme files and widget styles before 0.2, and reported 11 defects (four major, seven minor). All are fixed. Three were older than the theme work: the X11 DPI setting, unchecked window sizes, and colours out of range reaching the canvas. Those three are in 0.1 too, and 0.1.2 shipped their fixes, with the same code. This round the regression tests were written with the fixes, so instead of each being seen to fail first, an injected bug that undoes each fix is caught by its test.

| # | Defect | Fix | Checked by |
|---|---|---|---|
| 1 | A `widgets` block of 65,000 unknown property names made about two billion comparisons: unknown names joined the check for names set twice. | Only names the schema knows are remembered. | Test |
| 2 | `load_theme` measured a file and then opened it again: a FIFO put in its place blocked the open forever, and a file that grew was read only up to its old size. | The file is opened once, without blocking; anything but a regular file is refused; it is read to its end, under 1 MB. | Test: a FIFO on Linux, `NUL` on Windows |
| 3 | `App::set_theme` assigned the new theme before copying it had succeeded: running out of memory there left new colours with old widget styles. | Everything that can fail happens first, and the commit cannot fail (checked at compile time). | By reading |
| 4 | X11 trusted an `Xft.dpi` of `inf` (or any huge number), and an infinite scale reached `lround` and the conversion of window sizes to integers. | The setting counts only as a finite number from 24 to 960. Every backend converts window sizes through one checked function, as those from `WindowOptions` were unchecked on every backend. | Test |
| 5 | An opaque `state.hover` gave secondary and ghost buttons a hover tint 150% opaque, which the canvas packed into a wrong colour. | The tint stops at opaque, and the canvas clamps every colour it draws (NaN to 0). | Test |
| 6 | A transparent switch knob showed its invisible colour under the pointer. | The built-in hover knob blends premultiplied; the Themes page lists it among the relative defaults. | Test |
| 7 | A button kept `size.control` as its height under any font, so a large font drew outside it, beyond what a redraw covered. | A button is at least as tall as its text. | Test |
| 8 | A reference loop through an inherited token was reported at the inherited token: a position in the built-in theme's file. | A loop is placed at the first token on it that the file itself sets. | Test |
| 9 | The gallery took its theme file's name from the narrow command line, which loses names outside the Windows code page. | It starts at `wmain` on Windows. | By hand: `smalti-theme`, which starts the same way, read a file named with a Chinese character and a Swedish letter |
| 10 | A lone carriage return did not end a line: a `//` comment ran on over the next line, and later positions were off. | LF, CR LF and a lone CR each end a line. | Test |
| 11 | Any other error stopped the loader before it looked for reference loops, though the Themes page promised every error. | Tokens are resolved anyway, those with wrong values left out, so loops are reported with the rest. | Test |

Timed redraws and live reload were then checked through the real event loops with a probe program: on Win32, WSLg and Xvfb, a widget asking to be redrawn every 500 ms for 3 s drew 7 or 8 times and used about a millisecond of CPU between draws, and a saved theme was drawn 50 to 52 ms after the save.

## Theme files, in beta

The built-in themes became files in `beta`, and every widget now draws through a style. Read through the new loader, the themes equal the 0.1 themes field for field (`theme_builtin_files_equal_the_0_1_themes_field_for_field`), and with both changes the gallery screenshots that `tools/screenshots.py` renders are byte-identical to those of 0.1.1 in all three themes. `smalti-theme check` and `gallery <file>` were run by hand on Windows with the examples on the [Themes](Themes.md) page, the widget styles one among them.
