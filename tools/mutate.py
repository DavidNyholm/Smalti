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
                "  n.min_size = {std::min(o.min_size.width, o.size.width),\n"
                "                std::min(o.min_size.height, o.size.height)};",
                "  n.min_size = o.min_size;",
            )
        ],
    ),
    (
        "core/widget.cpp",
        "a setter's cancelled press can free its widget mid-cancel",
        [
            (
                "  if (enabled_ == enabled) return;\n  const detail::DispatchScope scope(detail::app_state());  // the hooks below must not free this",
                "  if (enabled_ == enabled) return;",
            )
        ],
    ),
    (
        "core/window.cpp",
        "a paint that pumps a nested frame gets a frame drawn inside it",
        [("  if (in_draw) {\n    request_frame();\n    return;\n  }", "")],
    ),
    (
        "core/window.cpp",
        "another mouse button ends the press",
        [("      if (!pressing || button != pressed_button) {", "      if (!pressing) {")],
    ),
    (
        "core/window.cpp",
        "the children of a parent outside the repaint are skipped",
        [
            (
                "  if (overlaps(to_device({b.x - o, b.y - o, b.width + 2 * o, b.height + 2 * o}, scale), clip))\n    w.paint(p);",
                "  if (!overlaps(to_device({b.x - o, b.y - o, b.width + 2 * o, b.height + 2 * o}, scale), clip)) return;\n  w.paint(p);",
            )
        ],
    ),
    (
        "render/font.cpp",
        "kerning sums beyond 16 bits change once cached",
        [("  total = std::clamp(total, int{INT16_MIN}, int{INT16_MAX});", "")],
    ),
    (
        "render/text.cpp",
        "overlong UTF-8 forms and surrogates are accepted",
        [("  if (cp < kMinForExtraBytes[extra] ||", "  if (false ||")],
    ),
    (
        "render/text.cpp",
        "a giant glyph is rasterized whatever it costs",
        [("      within_raster_limit(outline_, t)\n", "      true\n")],
    ),
    (
        "core/layout.cpp",
        "a span over stretch columns ignores what sharing takes back",
        [
            (
                "      const float kept = share ? 1.0f - span_weight / weight_total : 1.0f;",
                "      const float kept = 1.0f;",
            )
        ],
    ),
    (
        "core/window.cpp",
        "a press its own handler cancelled is captured anyway",
        [
            (
                "      if (press_serial != interaction) break;  // nested dispatch ended it: it goes no further\n",
                "",
            ),
            (
                "      native->capture_mouse(true);\n      w->invalidate();\n      if (!eligible(*w)) cancel_press();",
                "      pressed = w->id();\n      native->capture_mouse(true);\n      w->invalidate();\n      if (!eligible(*w)) cancel_press();",
            ),
        ],
    ),
    (
        "core/window.cpp",
        "focus gain is reported after focus already moved on",
        [("  if (w && focused == id) w->on_focus_changed(true);", "  if (w) w->on_focus_changed(true);")],
    ),
    (
        "core/app.cpp",
        "a failure during run()'s startup cleanup is swallowed",
        [
            (
                "  s.reap_closed();\n  rethrow_pending();\n  if (s.windows.empty()) return s.exit_code;",
                "  s.reap_closed();\n  if (s.windows.empty()) return s.exit_code;",
            )
        ],
    ),
    (
        "widgets/controls.cpp",
        "select() from code lets its callback free the radio",
        [
            (
                "  const detail::DispatchScope scope(detail::app_state());  // its callback must not free this radio\n",
                "",
            )
        ],
    ),
    (
        "core/window.cpp",
        "a press that nested dispatch ended bubbles on to the parent",
        [
            (
                "      if (press_serial != interaction) break;  // nested dispatch ended it: it goes no further\n",
                "",
            )
        ],
    ),
    (
        "core/window.cpp",
        "a widget is told it gained the hover after the hover moved on",
        [("  if (hovered != id) return;\n", "")],
    ),
    (
        "core/layout.cpp",
        "spanning children are measured against stale column widths",
        [("      result = shared_out();  // the next span sees what this one took\n", "")],
    ),
    (
        "core/testing.cpp",
        "the test driver swallows exceptions from callbacks",
        [
            (
                "  if (!s.pending_exception) return;\n  const std::exception_ptr e",
                "  return;\n  const std::exception_ptr e",
            )
        ],
    ),
    (
        "core/painter.cpp",
        "a zero-width border grows with the scale",
        [
            (
                "  const float w = width > 0 ? width * s_.scale : 1.0f;",
                "  const float w = width > 0 ? width * s_.scale : std::max(1.0f, std::floor(s_.scale));",
            )
        ],
    ),
    (
        "render/canvas.cpp",
        "the shadow corner cache never evicts",
        [("  while (shadow_bytes_ > kShadowCacheBudget && shadow_lru_.size() > 1) {", "  while (false) {")],
    ),
    (
        "widgets/controls.cpp",
        "selecting an already selected radio leaves a second selection in place",
        [
            (
                "  if (!selected) return;\n  // Also when it was selected already",
                "  if (!selected || !changed) return;\n  // Also when it was selected already",
            )
        ],
    ),
    (
        "widgets/controls.cpp",
        "a keyboard click shows no press",
        [("  press_ = Motion{1, 1, 0, 0, press_.easing};", "  press_.from = 1;")],
    ),
    (
        "core/window.cpp",
        "a press released by a focus hook still reaches a widget",
        [("    if (press_serial != interaction) return;  // released already\n", "")],
    ),
    (
        "core/window.cpp",
        "a widget's own hover hook can leave its cursor behind",
        [
            (
                "  if (hovered != id) return;  // its own hook moved the hover on (or hid it): the cursor is set\n",
                "",
            )
        ],
    ),
    (
        "core/testing.cpp",
        "the test driver's idle() swallows exceptions from cleanup",
        [
            (
                "  backend().host().after_events();\n  rethrow_failure();\n}\n\nint Driver::width",
                "  backend().host().after_events();\n}\n\nint Driver::width",
            )
        ],
    ),
    (
        "render/text.cpp",
        "text far off the canvas ends where it started",
        [
            (
                "  const int baseline = row_drawable ? static_cast<int>(std::lround(origin.y)) : 0;",
                "  if (!row_drawable) return origin.x;\n  const int baseline = static_cast<int>(std::lround(origin.y));",
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
            ("      main_len += extra * child->stretch() / total_stretch;", "      main_len += 0;"),
        ],
    ),
    (
        "core/layout.cpp",
        "justify end is ignored",
        [
            (
                "    else if (o.justify == Align::end)\n      pos += extra;",
                "    else if (o.justify == Align::end)\n      pos += 0;",
            ),
        ],
    ),
    (
        "core/layout.cpp",
        "centred children sit at the start",
        [
            ("        cross_pos += (inner_cross - cross_len) * 0.5f;\n", ""),
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
            ("        radio->set_selected(false, false);", "        (void)radio;"),
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
        [("    : options_(normalized(options)),", "    : options_(options),")],
    ),
    (
        "widgets/range.cpp",
        "the slider ignores its step",
        [
            (
                "  if (options_.step > 0)\n    v = options_.min + std::round((v - options_.min) / options_.step) * options_.step;\n",
                "",
            ),
        ],
    ),
    (
        "widgets/range.cpp",
        "Home jumps to the maximum",
        [
            (
                "    case Key::home:\n      change(options_.min);",
                "    case Key::home:\n      change(options_.max);",
            ),
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
            ("    if (!w || !w->visible() || frame_time >= it->second)", "    if (!w)"),
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
                "          last_space != std::string_view::npos && last_space > line_start ? last_space : i;",
                "          i;",
            ),
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
    # Theme files
    (
        "theme/jsonc.cpp",
        "the theme reader accepts nesting deeper than its limit",
        [
            (
                "  bool read_object(JsonValue& out, int depth) {\n    out.kind = JsonKind::object;\n    if (depth > kMaxJsonDepth) return fail(too_deep());",
                "  bool read_object(JsonValue& out, int depth) {\n    out.kind = JsonKind::object;\n    if (depth > kMaxJsonDepth * 4) return fail(too_deep());",
            )
        ],
    ),
    (
        "theme/jsonc.cpp",
        "the theme reader takes strings of any length",
        [("      if (out.size() > kMaxJsonStringBytes)", "      if (false)")],
    ),
    (
        "theme/jsonc.cpp",
        "the theme reader takes any number of values",
        [("    if (++values_ > kMaxJsonValues)", "    if (++values_ > kMaxJsonValues * 64)")],
    ),
    (
        "theme/jsonc.cpp",
        "the theme reader takes overlong UTF-8",
        [("  if (cp < smallest || cp > 0x10FFFF", "  if (cp > 0x10FFFF")],
    ),
    (
        "theme/jsonc.cpp",
        "theme error columns count bytes instead of characters",
        [
            (
                "      i_ += utf8_length(s_, i_);\n      ++pos_.column;",
                "      pos_.column += static_cast<int>(utf8_length(s_, i_));\n      i_ += utf8_length(s_, i_);",
            )
        ],
    ),
    (
        "theme/jsonc.cpp",
        "a trailing comma in a theme object is refused",
        [
            (
                "        if (peek() == '}') {  // a trailing comma\n          step();\n          return true;\n        }\n",
                "",
            )
        ],
    ),
    (
        "theme/tokens.cpp",
        "a theme opacity replaces a hex colour's own alpha",
        [("e.literal.color = c->with_alpha(c->a * opacity);", "e.literal.color = c->with_alpha(opacity);")],
    ),
    (
        "theme/tokens.cpp",
        "theme sizes are not range-checked",
        [("        if (!(v.number >= k.min && v.number <= k.max)) {", "        if (false) {")],
    ),
    (
        "theme/tokens.cpp",
        "a negative theme duration is accepted",
        [('!= "ms" || ms < 0 ||', '!= "ms" ||')],
    ),
    (
        "theme/tokens.cpp",
        "theme suggestions miss two swapped letters",
        [
            (
                "      if (i > 1 && j > 1 && lower(a[i - 1]) == lower(b[j - 2]) &&",
                "      if (false && lower(a[i - 1]) == lower(b[j - 2]) &&",
            )
        ],
    ),
    (
        "theme/tokens.hpp",
        "theme errors are not capped",
        [("    if (list_.size() < kMaxThemeErrors) {", "    if (true) {")],
    ),
    (
        "theme/theme.cpp",
        "a theme reference with an opacity ignores the opacity",
        [("      values_[i].color.a *= e.opacity;\n", "")],
    ),
    (
        "theme/theme.cpp",
        "a theme token set twice is not reported",
        [("      errors.add(m.pos, set_twice(m.key, *seen[*i]));\n      continue;", "      continue;")],
    ),
    (
        "theme/theme.cpp",
        "a theme with errors still replaces the base",
        [
            (
                "    out.theme = builtin_theme(doc.base == Base::none ? Base::dark : doc.base);",
                "    out.theme = theme_black();",
            )
        ],
    ),
    (
        "theme/widget_styles.cpp",
        "a widget style rule for every variant beats one for a single variant",
        [
            (
                "    for (const bool one_variant : {false, true}) {",
                "    for (const bool one_variant : {true, false}) {",
            )
        ],
    ),
    (
        "theme/widget_styles.cpp",
        "a disabled widget style does not dim",
        [("    c = d ? *d : c.with_alpha(c.a * disabled_opacity);", "    c = d ? *d : c;")],
    ),
    (
        "theme/widget_styles.cpp",
        "the widget style press shade ignores the hover fill",
        [("over(hovered ? *hovered : rest(s, S::fill), kPressShade)", "over(rest(s, S::fill), kPressShade)")],
    ),
    (
        "theme/widget_styles.cpp",
        "widget style colours of different opacity mix unpremultiplied",
        [("  if (from.a == to.a) return Color::mix(from, to, t);", "  return Color::mix(from, to, t);")],
    ),
    (
        "theme/widget_styles.cpp",
        "a widget style's focused colour never shows",
        [("f && mix.focused)", "f && false)")],
    ),
    (
        "theme/widgets_section.cpp",
        "a widget style may set a state the widget does not have",
        [("    if (!(schema_->states & state_bit(state))) {", "    if (false) {")],
    ),
    (
        "theme/theme.cpp",
        "a widget style rule naming a token drops its opacity",
        [("    v.color.a *= p.expr.opacity;\n", "")],
    ),
    (
        "widgets/label.cpp",
        "every label draws with the body text's widget style",
        [
            (
                "detail::StyledWidget::label, static_cast<size_t>(options_.role))",
                "detail::StyledWidget::label, 0)",
            )
        ],
    ),
    (
        "theme/widgets_section.cpp",
        "round 6: unknown widget properties join the duplicate check",
        [
            (
                "      const bool known = s || find_property(*schema_, state, m.key);",
                "      const bool known = true;",
            )
        ],
    ),
    (
        "platform/read_file_win32.cpp",
        "round 6: a theme file may be a pipe or a device",
        [("  if (GetFileType(file.get()) != FILE_TYPE_DISK) return {ReadStatus::not_a_file, {}};\n", "")],
    ),
    (
        "platform/backend.cpp",
        "round 6: any positive dpi is trusted",
        [("!std::isfinite(dpi) || dpi < kMinDpi || dpi > kMaxDpi", "dpi <= 0")],
    ),
    (
        "render/canvas.cpp",
        "round 6: fill_rect takes colours out of range",
        [
            (
                "void Canvas::fill_rect(RectF r, Color c) {\n  c = unit(c);\n",
                "void Canvas::fill_rect(RectF r, Color c) {\n",
            )
        ],
    ),
    (
        "theme/widget_styles.cpp",
        "round 6: the hover tint may pass opaque",
        [
            (
                "th.hover.with_alpha(std::min(1.0f, th.hover.a * kHoverTintStrength))",
                "th.hover.with_alpha(th.hover.a * kHoverTintStrength)",
            )
        ],
    ),
    (
        "theme/widget_styles.cpp",
        "round 6: the switch's hover knob mixes straight",
        [
            (
                "blend_colors(rest(s, S::knob), th.text, kSwitchKnobHoverMix)",
                "Color::mix(rest(s, S::knob), th.text, kSwitchKnobHoverMix)",
            )
        ],
    ),
    (
        "widgets/controls.cpp",
        "round 6: a button is never taller than size.control",
        [("std::max(th.control_height, detail::text_line_height(st))", "th.control_height")],
    ),
    (
        "theme/theme.cpp",
        "round 6: a loop is placed at the base theme's tokens",
        [("      from_file[i] = true;\n", "")],
    ),
    (
        "theme/jsonc.cpp",
        "round 6: a lone carriage return does not end a line",
        [("    if (c == '\\n' || (c == '\\r' && peek_at(1) != '\\n')) {", "    if (c == '\\n') {")],
    ),
    (
        "theme/theme.cpp",
        "round 6: other errors hide reference loops",
        [
            (
                "  Resolver resolver(exprs, from_file, errors);",
                "  if (errors.any()) return std::nullopt;\n  Resolver resolver(exprs, from_file, errors);",
            )
        ],
    ),
    (
        "platform/backend.cpp",
        "round 6: a NaN window size reaches lround",
        [("  if (!(v >= 1)) return 1;  // also NaN", "  if (v < 1) return 1;")],
    ),
    (
        "render/canvas.cpp",
        "round 6: shapes take colours out of range",
        [("  if (m.empty()) return;\n  c = unit(c);\n", "  if (m.empty()) return;\n")],
    ),
    (
        "core/widget.cpp",
        "a redraw delay too long for the clock overflows",
        [("delay.count() > INT64_MAX - now ? INT64_MAX : now + delay.count()", "now + delay.count()")],
    ),
    (
        "core/window.cpp",
        "a later timed redraw replaces an earlier one",
        [("      when = std::min(when, time_ns);", "      when = time_ns;")],
    ),
    (
        "platform/headless.cpp",
        "the headless clock never runs timed work",
        [("    host_.run_timers(now_);\n", "")],
    ),
    (
        "core/app.cpp",
        "a theme reloads at once instead of after the pause",
        [("backend->now_ns() + kThemeReloadDelayNs", "backend->now_ns()")],
    ),
    (
        "platform/file_watch_win32.cpp",
        "any file changing in the folder reloads the theme",
        [("    changed = s.names_file(bytes);", "    changed = true;")],
    ),
    (
        "core/app.cpp",
        "stopping the theme watch leaves the folder watched",
        [("  backend->stop_watching_file();\n", "")],
    ),
    (
        "platform/text_input.cpp",
        "typed text keeps control characters",
        [
            (
                "bool is_control(uint32_t cp) { return cp < 0x20 || (cp >= 0x7F && cp <= 0x9F); }",
                "bool is_control(uint32_t) { return false; }",
            )
        ],
    ),
    (
        "platform/text_input.cpp",
        "typed text drops both halves of a surrogate pair",
        [("    if (high_ != 0) {", "    if (false) {")],
    ),
    (
        "core/window.cpp",
        "typed text: the IME opens for any focused widget",
        [
            (
                "  const bool active = f && eligible(*f) && f->accepts_text();",
                "  const bool active = f && eligible(*f);",
            )
        ],
    ),
    (
        "core/window.cpp",
        "typed text: the IME stays where the caret was before a new layout",
        [("  sync_text_input();  // a new layout can move the caret without the widget saying so\n", "")],
    ),
    (
        "platform/text_input.cpp",
        "clipboard text: CRLF becomes two line breaks",
        [
            (
                "if (i + 1 < utf8.size() && utf8[i + 1] == '\\n') ++i;",
                "if (i + 1 < utf8.size() && utf8[i + 1] == '\\n') {}",
            )
        ],
    ),
    (
        "platform/text_input.cpp",
        "clipboard text keeps NUL characters",
        [("    } else if (cp != 0) {", "    } else {")],
    ),
    (
        "platform/text_input.cpp",
        "clipboard text: invalid bytes become '?' instead of U+FFFD",
        [("constexpr uint32_t kReplacement = 0xFFFD;", "constexpr uint32_t kReplacement = 0x3F;")],
    ),
    (
        "platform/text_input.cpp",
        "clipboard text for Windows keeps bare LF",
        [("if (c == '\\n') out += '\\r';", "if (c == '\\n') {}")],
    ),
    (
        "platform/text_input.cpp",
        "Latin-1 loses its last character, U+00FF",
        [("cp <= 0xFF ? static_cast<char>(cp)", "cp < 0xFF ? static_cast<char>(cp)")],
    ),
    (
        "core/app.cpp",
        "clipboard: text of exactly the limit is refused",
        [("text->size() > detail::kMaxClipboardBytes", "text->size() >= detail::kMaxClipboardBytes")],
    ),
    (
        "core/app.cpp",
        "clipboard: pasted text is not made plain",
        [("  return detail::plain_text(*text);", "  return *text;")],
    ),
    (
        "core/app.cpp",
        "clipboard: copied text is not made plain",
        [("set_clipboard_text(detail::plain_text(utf8));", "set_clipboard_text(utf8);")],
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
