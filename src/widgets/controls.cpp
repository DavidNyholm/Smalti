// Button, CheckBox, Switch and RadioButton.
#include <algorithm>

#include "smalti/widgets.hpp"
#include "widgets/style.hpp"

namespace smalti {

namespace {

constexpr float kButtonPadding = 14;
constexpr float kPressDarken = 0.12f;  // how much a pressed button darkens
constexpr float kBoxSize = 18;
constexpr float kBoxRadius = 5;
constexpr float kOutlineWidth = 1.5f;
constexpr float kTrackWidth = 36, kTrackHeight = 20, kKnobRadius = 7;
constexpr float kRadioRadius = 9, kRadioDot = 4.5f;

bool activates(const KeyEvent& e) { return e.down && !e.repeat && (e.key == Key::space || e.key == Key::enter); }

bool primary_press(const MouseEvent& e) {
  return e.kind == MouseEvent::Kind::press && e.button == MouseButton::left;
}

bool primary_release(const MouseEvent& e) {
  return e.kind == MouseEvent::Kind::release && e.button == MouseButton::left;
}

}  // namespace

// --- Button -------------------------------------------------------------------------------

Button::Button(ButtonOptions options) : options_(std::move(options)) {}

Button& Button::on_click(Callback<void()> fn) {
  on_click_ = std::move(fn);
  return *this;
}

void Button::set_text(std::string text) {
  options_.text = std::move(text);
  invalidate_layout();
}

Size Button::measure(Size) {
  const Theme& th = theme();
  return {detail::measure_text(options_.text, detail::body_text(th, FontWeight::medium)) + 2 * kButtonPadding,
          th.control_height};
}

void Button::paint(Painter& p) {
  const Theme& th = theme();
  const Rect r = bounds();
  const float hover = at(hover_), press = at(press_);
  const bool off = !enabled();
  Color fill{0, 0, 0, 0}, fg = th.text;
  switch (options_.text.empty() ? ButtonVariant::ghost : options_.variant) {
    case ButtonVariant::primary:
      fill = Color::mix(th.accent, th.accent_hover, hover);
      fg = th.on_accent;
      break;
    case ButtonVariant::secondary:
      fill = th.raised;
      break;
    case ButtonVariant::ghost:
      fg = th.accent;
      break;
    case ButtonVariant::danger:
      fill = th.danger.with_alpha(0.14f + 0.06f * hover);
      fg = th.danger;
      break;
  }
  if (fill.a > 0) p.fill_rounded_rect(r, th.radius_control, detail::dim_if(off, fill));
  if (options_.variant == ButtonVariant::secondary) p.stroke_rounded_rect(r, th.radius_control, 0, th.border);
  if (options_.variant != ButtonVariant::primary && options_.variant != ButtonVariant::danger && hover > 0) {
    p.fill_rounded_rect(r, th.radius_control, th.hover.with_alpha(th.hover.a * hover * 1.5f));
  }
  if (press > 0) p.fill_rounded_rect(r, th.radius_control, Color{0, 0, 0, kPressDarken * press});
  const TextStyle st = detail::body_text(th, FontWeight::medium);
  const float w = p.text_width(options_.text, st);
  p.text({r.x + (r.width - w) * 0.5f, detail::centered_baseline(r, st)}, options_.text, st, detail::dim_if(off, fg));
  if (focus_visible()) detail::focus_ring(p, r, th.radius_control, th);
}

void Button::press_visual(bool down) { animate(press_, down ? 1.0f : 0.0f, theme().motion_fast); }

void Button::click() {
  if (on_click_) on_click_();
}

bool Button::on_mouse(const MouseEvent& e) {
  if (primary_press(e)) {
    press_visual(true);
    return true;
  }
  if (e.kind == MouseEvent::Kind::cancel) {
    press_visual(false);
    return true;
  }
  if (primary_release(e)) {
    press_visual(false);
    if (bounds().contains(e.position)) click();
    return true;
  }
  return false;
}

bool Button::on_key(const KeyEvent& e) {
  if (!activates(e)) return false;
  // A brief press so the keyboard click is visible, then the action.
  press_.from = 1;
  animate(press_, 0.0f, theme().motion_base);
  click();
  return true;
}

void Button::on_hover_changed(bool hovered) { animate(hover_, hovered ? 1.0f : 0.0f, theme().motion_fast); }

void Button::on_focus_changed(bool) { invalidate(); }

// --- CheckBox -----------------------------------------------------------------------------

CheckBox::CheckBox(CheckBoxOptions options)
    : text_(std::move(options.text)), checked_(options.checked) {
  check_.to = checked_ ? 1.0f : 0.0f;
}

CheckBox& CheckBox::on_change(Callback<void(bool)> fn) {
  on_change_ = std::move(fn);
  return *this;
}

void CheckBox::set_checked(bool checked) {
  if (checked == checked_) return;
  checked_ = checked;
  animate(check_, checked ? 1.0f : 0.0f, theme().motion_fast);
}

void CheckBox::toggle() {
  set_checked(!checked_);
  if (on_change_) on_change_(checked_);
}

Size CheckBox::measure(Size) {
  const Theme& th = theme();
  const float text = text_.empty() ? 0 : detail::kLabelGap + detail::measure_text(text_, detail::body_text(th));
  return {kBoxSize + text, std::max(kBoxSize, detail::text_line_height(detail::body_text(th)))};
}

void CheckBox::paint(Painter& p) {
  const Theme& th = theme();
  const Rect r = bounds();
  const bool off = !enabled();
  const float t = at(check_), hover = at(hover_);
  const Rect box{r.x, r.y + (r.height - kBoxSize) * 0.5f, kBoxSize, kBoxSize};
  p.fill_rounded_rect(box, kBoxRadius, detail::dim_if(off, Color::mix(th.raised, th.accent, t)));
  if (t < 1) {
    const Color edge = Color::mix(th.text_muted, th.text_secondary, hover);
    p.stroke_rounded_rect(box, kBoxRadius, kOutlineWidth, detail::dim_if(off, edge.with_alpha(edge.a * (1 - t))));
  }
  if (t > 0) {
    // The check mark draws in as the box fills: its second stroke grows with t.
    const Point a{box.x + 4.5f, box.y + 9.5f}, b{box.x + 7.5f, box.y + 12.5f}, c{box.x + 13.5f, box.y + 6.0f};
    const Point tip{b.x + (c.x - b.x) * t, b.y + (c.y - b.y) * t};
    const Point mark[] = {a, b, tip};
    p.polyline(mark, 2.0f, detail::dim_if(off, th.on_accent.with_alpha(t)));
  }
  if (!text_.empty()) {
    const TextStyle st = detail::body_text(th);
    p.text({box.right() + detail::kLabelGap, detail::centered_baseline(r, st)}, text_, st, detail::dim_if(off, th.text));
  }
  if (focus_visible()) detail::focus_ring(p, box, kBoxRadius, th);
}

bool CheckBox::on_mouse(const MouseEvent& e) {
  if (primary_press(e)) return true;
  if (primary_release(e)) {
    if (bounds().contains(e.position)) toggle();
    return true;
  }
  return false;
}

bool CheckBox::on_key(const KeyEvent& e) {
  if (!e.down || e.repeat || e.key != Key::space) return false;
  toggle();
  return true;
}

void CheckBox::on_hover_changed(bool hovered) { animate(hover_, hovered ? 1.0f : 0.0f, theme().motion_fast); }

// --- Switch -------------------------------------------------------------------------------

Switch::Switch(SwitchOptions options) : text_(std::move(options.text)), on_(options.on) {
  knob_.to = on_ ? 1.0f : 0.0f;
}

Switch& Switch::on_change(Callback<void(bool)> fn) {
  on_change_ = std::move(fn);
  return *this;
}

void Switch::set_on(bool on) {
  if (on == on_) return;
  on_ = on;
  animate(knob_, on ? 1.0f : 0.0f, theme().motion_base);
}

void Switch::toggle() {
  set_on(!on_);
  if (on_change_) on_change_(on_);
}

Size Switch::measure(Size) {
  const Theme& th = theme();
  const float text = text_.empty() ? 0 : detail::kLabelGap + detail::measure_text(text_, detail::body_text(th));
  return {kTrackWidth + text, std::max(kTrackHeight, detail::text_line_height(detail::body_text(th)))};
}

void Switch::paint(Painter& p) {
  const Theme& th = theme();
  const Rect r = bounds();
  const bool off = !enabled();
  const float t = at(knob_), hover = at(hover_);
  const Rect track{r.x, r.y + (r.height - kTrackHeight) * 0.5f, kTrackWidth, kTrackHeight};
  const float radius = kTrackHeight * 0.5f;
  p.fill_rounded_rect(track, radius, detail::dim_if(off, Color::mix(th.selected, th.accent, t)));
  if (t < 1) p.stroke_rounded_rect(track, radius, 0, th.border.with_alpha(th.border.a * (1 - t)));
  const float travel = kTrackWidth - 2 * radius;
  const Point knob{track.x + radius + travel * t, track.y + radius};
  const Color knob_color = Color::mix(Color::mix(th.text_secondary, th.text, hover * 0.5f), th.on_accent, t);
  p.fill_circle(knob, kKnobRadius, detail::dim_if(off, knob_color));
  if (!text_.empty()) {
    const TextStyle st = detail::body_text(th);
    p.text({track.right() + detail::kLabelGap, detail::centered_baseline(r, st)}, text_, st, detail::dim_if(off, th.text));
  }
  if (focus_visible()) detail::focus_ring(p, track, radius, th);
}

bool Switch::on_mouse(const MouseEvent& e) {
  if (primary_press(e)) return true;
  if (primary_release(e)) {
    if (bounds().contains(e.position)) toggle();
    return true;
  }
  return false;
}

bool Switch::on_key(const KeyEvent& e) {
  if (!activates(e)) return false;
  toggle();
  return true;
}

void Switch::on_hover_changed(bool hovered) { animate(hover_, hovered ? 1.0f : 0.0f, theme().motion_fast); }

// --- RadioButton --------------------------------------------------------------------------

RadioButton::RadioButton(std::string text, bool selected) : text_(std::move(text)), selected_(selected) {
  dot_.to = selected ? 1.0f : 0.0f;
}

RadioButton& RadioButton::on_select(Callback<void()> fn) {
  on_select_ = std::move(fn);
  return *this;
}

void RadioButton::select() { set_selected(true, true); }

void RadioButton::set_selected(bool selected, bool notify) {
  if (selected == selected_) return;
  selected_ = selected;
  animate(dot_, selected ? 1.0f : 0.0f, theme().motion_fast);
  if (!selected) return;
  // The rest of the group lets go.
  if (Widget* group = parent().try_get()) {
    for (auto& sibling : group->children()) {
      if (auto* radio = dynamic_cast<RadioButton*>(sibling.get()); radio && radio != this) radio->set_selected(false, false);
    }
  }
  if (notify && on_select_) on_select_();
}

// Only a selection the keyboard can reach counts: with the selected radio hidden or disabled,
// every other radio in the group is a tab stop again.
bool RadioButton::group_has_selection() const {
  const Widget* parent = this->parent().try_get();
  if (!parent) return selected_;
  for (const auto& sibling : parent->children()) {
    const auto* radio = dynamic_cast<const RadioButton*>(sibling.get());
    if (radio && radio->selected_ && radio->visible() && radio->enabled()) return true;
  }
  return false;
}

void RadioButton::move_selection(int step) {
  Widget* parent = this->parent().try_get();
  if (!parent) return;
  std::vector<RadioButton*> group;
  for (auto& sibling : parent->children()) {
    if (auto* radio = dynamic_cast<RadioButton*>(sibling.get()); radio && radio->visible() && radio->enabled()) {
      group.push_back(radio);
    }
  }
  const auto it = std::find(group.begin(), group.end(), this);
  if (it == group.end() || group.size() < 2) return;
  const size_t i = static_cast<size_t>(it - group.begin());
  RadioButton* next = group[(i + group.size() + static_cast<size_t>(step)) % group.size()];
  next->select();
  next->focus();
}

Size RadioButton::measure(Size) {
  const Theme& th = theme();
  const float text = text_.empty() ? 0 : detail::kLabelGap + detail::measure_text(text_, detail::body_text(th));
  return {2 * kRadioRadius + text, std::max(2 * kRadioRadius, detail::text_line_height(detail::body_text(th)))};
}

void RadioButton::paint(Painter& p) {
  const Theme& th = theme();
  const Rect r = bounds();
  const bool off = !enabled();
  const float t = at(dot_), hover = at(hover_);
  const Point c{r.x + kRadioRadius, r.y + r.height * 0.5f};
  p.fill_circle(c, kRadioRadius, th.raised);
  const Color ring = Color::mix(Color::mix(th.text_muted, th.text_secondary, hover), th.accent, t);
  p.stroke_circle(c, kRadioRadius, kOutlineWidth, detail::dim_if(off, ring));
  if (t > 0) p.fill_circle(c, kRadioDot * t, detail::dim_if(off, th.accent));
  if (!text_.empty()) {
    const TextStyle st = detail::body_text(th);
    p.text({r.x + 2 * kRadioRadius + detail::kLabelGap, detail::centered_baseline(r, st)}, text_, st,
           detail::dim_if(off, th.text));
  }
  if (focus_visible()) {
    detail::focus_ring(p, {c.x - kRadioRadius, c.y - kRadioRadius, 2 * kRadioRadius, 2 * kRadioRadius}, kRadioRadius, th);
  }
}

bool RadioButton::on_mouse(const MouseEvent& e) {
  if (primary_press(e)) return true;
  if (primary_release(e)) {
    if (bounds().contains(e.position)) select();
    return true;
  }
  return false;
}

bool RadioButton::on_key(const KeyEvent& e) {
  if (!e.down) return false;
  switch (e.key) {
    case Key::space: select(); return true;
    case Key::right:
    case Key::down: move_selection(1); return true;
    case Key::left:
    case Key::up: move_selection(-1); return true;
    default: return false;
  }
}

void RadioButton::on_hover_changed(bool hovered) { animate(hover_, hovered ? 1.0f : 0.0f, theme().motion_fast); }

}  // namespace smalti
