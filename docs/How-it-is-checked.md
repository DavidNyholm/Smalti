# How it is checked

There is no continuous integration yet. Every check on this page runs locally, with the commands given, on Windows with MSVC 2022 and on Linux (Ubuntu 24.04 under WSL) with GCC 14. A version is tagged only after all of them pass.

## The tests

`smalti_tests` holds 63 tests and runs in a few seconds. It needs no test framework, and no screen: most tests drive real widgets through the headless backend, with simulated mouse and keyboard input, a fake clock that advances one frame at a time, and the drawn pixels read back.

- **Rendering:** the rasterizer's coverage is exact (rectangles with fractional edges, a circle's area, holes from opposite winding, clipping), soft shadows' fast path matches the direct integral to within one step in 255, dithered gradients average to the true ramp.
- **Fonts and text:** Inter's metrics and kerning. Truncated and randomly damaged copies of the font (about 300) must either be refused cleanly or still work. Fonts built byte by byte in `tests/font_builder.hpp` stand in for hostile ones: a composite glyph that multiplies itself, a character map that lies about its size, kerning split across lookups.
- **Lifetimes:** dead `Ref`s are reported and never dereferenced, slot reuse does not revive old `Ref`s, widgets remove themselves from their own callbacks, calls from another thread are refused, 2,000 rounds of building and removing controls and 300 windows opened and closed return to the starting widget count.
- **Layout, widgets and motion:** stacks, grids, every control by mouse and keyboard, focus order, animations that settle and then request no more frames.
- **Memory:** an `operator new` counter shows that an animated frame allocates nothing once warm.
- **Review regressions:** `tests/test_review.cpp`, one test per defect found in review (below).

```bash
cmake --preset windows
cmake --build --preset windows
build/windows/Debug/smalti_tests
```

On Linux, the `linux` preset and `./build/linux/smalti_tests`. A name fragment as the first argument runs only the matching tests: `smalti_tests review_`.

## Warnings

Both compilers build with strict warnings (`/W4 /permissive-` on MSVC, `-Wall -Wextra -Wpedantic` on GCC), in Debug and Release, without a single warning.

## Sanitizers

The `linux-asan` preset builds everything with AddressSanitizer, UndefinedBehaviorSanitizer and LeakSanitizer. Undefined behaviour stops the run rather than printing a note. The whole suite passes clean. Several of the review regressions only fail loudly under it: a read past the end of a buffer or a use after free can go unnoticed in a normal build.

```bash
cmake --preset linux-asan
cmake --build --preset linux-asan
./build/linux-asan/smalti_tests
```

## Injected bugs

A test suite that passes proves little until it is seen to fail. `tools/mutate.py` puts one deliberate bug at a time into the sources, rebuilds and runs the suite, and reports whether any test failed. The bugs are the plausible ones: a stale `Ref` resolving to whatever reuses its slot, removed widgets never destroyed, damage never recorded, Shift+Tab moving forwards, the glyph cache never evicting, a shadow tile read one pixel off, kerning stopping after the first lookup, and so on, plus ten that each undo one of the review fixes below.

Result on the current sources, MSVC Debug: **42 of 42 caught**, by a failing test or a crashing test binary, none by a time-out. The first run after the review fixes caught 37. Every miss involved an older test or an injected bug, not a review fix, and each has been put right:

- Shift+Tab moving forwards went unseen because the test had only two focusable widgets, where one step back and one step forward land on the same one.
- One injected bug removed a clamp that a second clamp makes redundant, so it changed nothing. It now removes the split of an edge where it crosses the clip's border, and the test's edge crosses that border halfway down a pixel row, where the split matters (before, it crossed exactly on a row boundary, where it does not).
- A UTF-8 lead byte followed by a plain letter was never tested.
- Removed widgets that are never destroyed made the churn test grind through all 2,000 rounds until it timed out; it now stops at the first leaked round.
- One injected bug itself was faulty: on MSVC, `v = {}` keeps a vector's memory, so it changed nothing.

```bash
python tools/mutate.py --preset windows
python tools/mutate.py --preset windows --only kerning
```

The second form runs only the bugs whose description contains the given text. A time-out counts as caught, since a suite that no longer finishes is a failure anyone sees, but it is reported separately.

## Review

Before its release, v0.1 had an independent code review: a read-only pass over the whole source by a reviewer asked to act as a hostile senior engineer, with each finding stated as a concrete failure scenario. It reported 25 defects: one blocker, twelve major, twelve minor. All 25 are fixed. Where a defect could be reproduced without a screen, a regression test was written first and seen to fail before the fix.

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
