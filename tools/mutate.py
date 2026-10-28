"""Tests against deliberate bugs.

Injects one bug at a time into the sources, rebuilds the test binary, runs it, and reports
whether the suite caught the bug. Every file is restored afterwards, whatever happens.

    python tools/mutate.py                   # the "windows" preset on Windows, "headless" elsewhere
    python tools/mutate.py --preset linux
    python tools/mutate.py --only kerning    # just the bugs whose description contains "kerning"

A suite that no longer finishes (TIMED OUT) counts as having caught the bug: anyone running it
sees the failure. It is reported separately all the same.

Needs the configure step to have been run for that preset (cmake --preset <name>).
"""

from __future__ import annotations

import argparse
import platform
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# (file under src/, description, [(text to find, replacement), ...]). Each text must occur once.
MUTATIONS = [
    # Refs and lifetimes
    (
        "core/registry.cpp",
        "a stale Ref resolves to whatever reuses its slot",
        [("return s.generation == id.generation ? s.widget : nullptr;", "return s.widget;")],
    ),
    (
        "core/registry.cpp",
        "calls from another thread are not refused",
        [("if (ui_thread_ && *ui_thread_ != std::this_thread::get_id()) {", "if (false) {")],
    ),
    (
        "core/widget.cpp",
        "add<T>() works on widgets that take no children",
        [("  if (!accepts_children()) {", "  if (false) {")],
    ),
    (
        "core/app.cpp",
        "removed widgets are never destroyed",
        [
            (
                "      Tree::detach(*w);  // the returned owner",
                "      // Tree::detach(*w);  // the returned owner",
            )
        ],
    ),
    (
        "core/app.cpp",
        "removals requested outside an event wait forever",
        [
            (
                "  flush_removals();  // removals requested outside any event",
                "  // removals requested outside any event",
            )
        ],
    ),
    (
        "core/app.cpp",
        "an exception from a callback is swallowed",
        [("  if (!pending_exception) pending_exception = e;", "  (void)e;")],
    ),
    (
        "core/app.cpp",
        "work posted from another thread never runs",
        [("    for (auto& fn : work) guarded([&] { fn(); });", "    (void)work;")],
    ),
    (
        "core/app.cpp",
        "a nested event loop destroys widgets whose handlers are still running",
        [("  if (dispatch_depth > 0) return;  // an outer event", "  // an outer event")],
    ),
    (
        "core/widget.cpp",
        "children built before their parent was added never learn their window",
        [("  attach(*child, window_);", "  detail::Tree::set_window(*child, window_);")],
    ),
    (
        "core/window.cpp",
        "damage requested while painting is dropped",
        [
            (
                "  native->present(drawing);\n  drawing.clear();",
                "  native->present(drawing);\n  drawing.clear();\n  dirty.clear();",
            )
        ],
    ),
    (
        "core/window.cpp",
        "a window with no widgets never draws its first frame",
        [("  invalidate_layout();  // the first frame", "  // the first frame")],
    ),
    (
        "core/window.cpp",
        "the default minimum size overrides a smaller requested size",
        [
            (
                "  n.min_size = {std::min(o.min_size.width, o.size.width), std::min(o.min_size.height, o.size.height)};",
                "  n.min_size = o.min_size;",
            )
        ],
    ),
    # Layout
    (
        "core/layout.cpp",
        "stacks ignore spacing between children",
        [("    pos += main_len + o.spacing;", "    pos += main_len;")],
    ),
    (
        "core/layout.cpp",
        "stretch factors get no leftover space",
        [
            (
                "if (total_stretch > 0 && child->stretch() > 0) main_len += extra * child->stretch() / total_stretch;",
                "if (total_stretch > 0 && child->stretch() > 0) main_len += 0;",
            )
        ],
    ),
    (
        "core/layout.cpp",
        "justify end is ignored",
        [
            (
                "    else if (o.justify == Align::end) pos += extra;",
                "    else if (o.justify == Align::end) pos += 0;",
            )
        ],
    ),
    (
        "core/layout.cpp",
        "centred children sit at the start",
        [
            (
                "      case Align::center: cross_pos += (inner_cross - cross_len) * 0.5f; break;",
                "      case Align::center: break;",
            )
        ],
    ),
    (
        "core/layout.cpp",
        "grid fit columns ignore their cells",
        [
            (
                "      widths[c] = std::max(widths[c], s.width);",
                "      (void)s;",
            )
        ],
    ),
    (
        "core/layout.cpp",
        "a spanning grid child does not widen its columns",
        [("      if (content_sized(c)) widths[c] += extra;", "      (void)extra;")],
    ),
    (
        "core/layout.cpp",
        "grid rows ignore row spacing",
        [
            (
                "tops[r] = tops[r - 1] + heights[r - 1] + options_.row_spacing;",
                "tops[r] = tops[r - 1] + heights[r - 1];",
            )
        ],
    ),
    # Input and widgets
    (
        "widgets/controls.cpp",
        "a button clicks even when released outside",
        [
            (
                "    press_visual(false);\n    if (bounds().contains(e.position)) click();",
                "    press_visual(false);\n    click();",
            )
        ],
    ),
    (
        "widgets/controls.cpp",
        "Enter does not press a button",
        [("(e.key == Key::space || e.key == Key::enter)", "(e.key == Key::space)")],
    ),
    (
        "widgets/controls.cpp",
        "selecting a radio button leaves the others selected",
        [
            (
                "radio && radio != this) radio->set_selected(false, false);",
                "radio && radio != this) (void)radio;",
            )
        ],
    ),
    (
        "widgets/controls.cpp",
        "a hidden selected radio still takes the group's tab stop",
        [
            (
                "if (radio && radio->selected_ && radio->visible() && radio->enabled()) return true;",
                "if (radio && radio->selected_) return true;",
            )
        ],
    ),
    (
        "widgets/range.cpp",
        "reversed slider bounds are put right only after the value is clamped",
        [("    : options_(normalized(options)), value_(", "    : options_(options), value_(")],
    ),
    (
        "widgets/range.cpp",
        "the slider ignores its step",
        [
            (
                "  if (options_.step > 0) v = options_.min + std::round((v - options_.min) / options_.step) * options_.step;\n",
                "",
            )
        ],
    ),
    (
        "widgets/range.cpp",
        "Home jumps to the maximum",
        [
            (
                "    case Key::home: change(options_.min); return true;",
                "    case Key::home: change(options_.max); return true;",
            )
        ],
    ),
    (
        "core/window.cpp",
        "Shift+Tab moves focus forwards",
        [
            (
                "next = backwards ? (i + order.size() - 1) % order.size() : (i + 1) % order.size();",
                "next = (i + 1) % order.size();",
            )
        ],
    ),
    (
        "core/window.cpp",
        "hover changes are not reported to the widget",
        [("  if (w) w->on_hover_changed(true);", "  (void)w;")],
    ),
    # Motion and damage
    (
        "core/window.cpp",
        "animations never end, so an idle window keeps drawing",
        [
            (
                "if (!w || !w->visible() || frame_time >= it->second) it = animating.erase(it);",
                "if (!w) it = animating.erase(it);",
            )
        ],
    ),
    (
        "core/window.cpp",
        "damage is never recorded",
        [
            (
                "  dirty.push_back(r);\n  if (dirty.size() > kMaxDirtyRects) {",
                "  if (dirty.size() > kMaxDirtyRects) {",
            )
        ],
    ),
    (
        "core/window.cpp",
        "the window scale is ignored",
        [("  scale = new_scale > 0 ? new_scale : 1.0f;", "  scale = 1.0f;")],
    ),
    # Rendering
    (
        "render/raster.cpp",
        "edges are not split where they cross the clip's left border",
        [("  for (float edge : {0.0f, w}) {", "  for (float edge : {w}) {")],
    ),
    (
        "render/raster.cpp",
        "curves are flattened coarsely",
        [("constexpr float kFlattenTolerance = 0.05f;", "constexpr float kFlattenTolerance = 1.0f;")],
    ),
    (
        "render/canvas.cpp",
        "the cached shadow corner is read one pixel off",
        [
            (
                "const int u = std::clamp(static_cast<int>(half_w - std::fabs(px) + origin), 0, tile - 1);",
                "const int u = std::clamp(static_cast<int>(half_w - std::fabs(px) + origin) + 1, 0, tile - 1);",
            )
        ],
    ),
    (
        "render/canvas.cpp",
        "gradients are not dithered",
        [
            (
                "      dst[x] = blend_float(dst[x], c, a, dither ? dither_noise(m.x + x, dy) : 0.5f);",
                "      dst[x] = blend_float(dst[x], c, a, 0.5f);",
            )
        ],
    ),
    (
        "render/geometry.cpp",
        "reused paths give their memory back, so every shape allocates",
        [
            (
                "void Path::clear() {\n  verbs_.clear();\n  points_.clear();",
                "void Path::clear() {\n  verbs_ = std::vector<Verb>();\n  points_ = std::vector<Point>();",
            )
        ],
    ),
    # Fonts and text
    (
        "render/font.cpp",
        "GPOS kerning is never read",
        [("        pair_subtables_.push_back(sub);", "        (void)sub;")],
    ),
    (
        "render/font.cpp",
        "kerning stops after the first lookup",
        [("      begin = end;\n    }", "      begin = end;\n      break;\n    }")],
    ),
    (
        "render/text.hpp",
        "glyph cache entries with no pixels cost nothing",
        [
            (
                "  static size_t cost(const Entry& e) { return e.coverage.size() + kEntryOverhead; }",
                "  static size_t cost(const Entry& e) { return e.coverage.size(); }",
            )
        ],
    ),
    (
        "render/text.cpp",
        "the glyph cache never evicts",
        [("  while (bytes_ > budget_ && lru_.size() > 1) {", "  while (false) {")],
    ),
    (
        "render/text.cpp",
        "wrapping breaks mid-word instead of at the last space",
        [
            (
                "const size_t end = last_space != std::string_view::npos && last_space > line_start ? last_space : i;",
                "const size_t end = i;",
            )
        ],
    ),
    (
        "render/text.cpp",
        "a UTF-8 sequence cut short is decoded as if complete",
        [
            (
                "    if (i >= s.size() || (static_cast<uint8_t>(s[i]) & 0xC0) != 0x80) return kReplacement;",
                "    if (i >= s.size()) return kReplacement;",
            )
        ],
    ),
]

BUILD_TIMEOUT_S = 900  # a mutant must not hang the whole run on a broken build
TEST_TIMEOUT_S = 180  # the suite takes a few seconds; a hang is a finding of its own


def run(args: list[str], timeout: int) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, cwd=ROOT, capture_output=True, text=True, timeout=timeout)


def build_and_test(build_args: list[str], binary: Path) -> tuple[str, list[str]]:
    """Returns (status, failing test names). status: caught, MISSED, BUILD FAILED, TIMED OUT."""
    try:
        build = run(build_args, BUILD_TIMEOUT_S)
    except subprocess.TimeoutExpired:
        return "TIMED OUT", []
    if build.returncode != 0:
        return "BUILD FAILED", []
    try:
        test = run([str(binary)], TEST_TIMEOUT_S)
    except subprocess.TimeoutExpired:
        return "TIMED OUT", []
    failing = [line.split()[1] for line in test.stdout.splitlines() if line.startswith("  FAIL")]
    return ("caught" if test.returncode != 0 else "MISSED"), failing


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    default_preset = "windows" if platform.system() == "Windows" else "headless"
    parser.add_argument("--preset", default=default_preset)
    parser.add_argument("--only", default="", help="run only the bugs whose description contains this")
    opts = parser.parse_args()
    build_dir = ROOT / "build" / opts.preset
    windows = opts.preset.startswith("windows")
    binary = build_dir / ("Debug/smalti_tests.exe" if windows else "smalti_tests")
    build_args = ["cmake", "--build", str(build_dir), "--target", "smalti_tests"]
    if windows:
        build_args += ["--config", "Debug"]

    # Baseline gate: a suite that already fails would "catch" every mutant for the wrong reason.
    status, failing = build_and_test(build_args, binary)
    if status != "MISSED":
        print(f"baseline is not green ({status} {', '.join(failing)}); fix that first")
        return 2
    print("baseline: all tests pass")

    results = []
    for file_name, description, edits in MUTATIONS:
        if opts.only not in description:
            continue
        path = ROOT / "src" / file_name
        original = path.read_text(encoding="utf-8")
        mutated = original
        applicable = True
        for old, new in edits:
            if mutated.count(old) != 1:
                applicable = False
                break
            mutated = mutated.replace(old, new)
        if not applicable:
            results.append(("NOT APPLIED", description, []))
            print(f"NOT APPLIED  {description}  (the text to mutate was not found exactly once)", flush=True)
            continue
        path.write_text(mutated, encoding="utf-8", newline="\n")
        try:
            status, failing = build_and_test(build_args, binary)
            results.append((status, description, failing))
            if failing:
                detail = ", ".join(failing)
            elif status == "caught":
                detail = "(the test binary crashed)"
            elif status == "MISSED":
                detail = "(every test passed)"
            else:
                detail = ""
            print(f"{status:<12} {description}  {detail}", flush=True)
        finally:
            path.write_text(original, encoding="utf-8", newline="\n")

    # Leave the build matching the restored sources, and say so if that fails.
    restored = run(build_args, BUILD_TIMEOUT_S)
    if restored.returncode != 0:
        print("WARNING: rebuilding the restored sources failed; run the build by hand")
        return 2
    caught = sum(1 for r in results if r[0] in ("caught", "TIMED OUT"))
    timed_out = sum(1 for r in results if r[0] == "TIMED OUT")
    note = f" ({timed_out} by the suite no longer finishing)" if timed_out else ""
    print(f"\n{caught} of {len(results)} injected bugs caught{note}")
    return 0 if caught == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
