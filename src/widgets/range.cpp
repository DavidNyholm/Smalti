// Slider, ProgressBar and Separator.
#include <algorithm>
#include <cmath>

#include "smalti/widgets.hpp"
#include "widgets/style.hpp"

namespace smalti {

namespace {

constexpr float kSliderHeight = 20, kTrackThickness = 4, kSliderKnob = 8;
constexpr float kKnobHoverGrowth = 1;  // the knob grows a little under the pointer
constexpr float kKnobShadowBlur = 2, kKnobShadowOffset = 1;
constexpr float kShadowReachBlurs = 3;  // a shadow fades out within 3 blurs (render/canvas.cpp)
constexpr float kSliderMinWidth = 120;
constexpr float kKeyStepFraction = 0.01f;  // arrow keys move 1% of the range without a step
constexpr float kPageStepFraction = 0.1f;  // Page Up and Page Down move 10%
constexpr float kProgressThickness = 6;
constexpr float kProgressMinWidth = 80;
constexpr float kSweepWidth = 0.3f;  // the indeterminate segment, as a share of the track
constexpr int64_t kSweepPeriodNs = 1'400'000'000;

}  // namespace

// --- Slider -------------------------------------------------------------------------------

namespace {

SliderOptions normalized(SliderOptions o) {
  if (o.max < o.min) std::swap(o.min, o.max);
  return o;
}

}  // namespace

Slider::Slider(SliderOptions options)
    : options_(normalized(options)),
      value_(std::clamp(options_.value, options_.min, options_.max)) {}

Slider& Slider::on_change(Callback<void(float)> fn) {
  on_change_ = std::move(fn);
  return *this;
}

void Slider::set_value(float v) {
  v = std::clamp(v, options_.min, options_.max);
  if (options_.step > 0)
    v = options_.min + std::round((v - options_.min) / options_.step) * options_.step;
  v = std::clamp(v, options_.min, options_.max);
  if (v == value_) return;
  value_ = v;
  invalidate();
}

void Slider::change(float v) {
  const float before = value_;
  set_value(v);
  if (value_ != before && on_change_) on_change_(value_);
}

float Slider::fraction() const {
  const float range = options_.max - options_.min;
  return range > 0 ? (value_ - options_.min) / range : 0.0f;
}

void Slider::set_from_pointer(float x) {
  const Rect r = bounds();
  const float usable = std::max(1.0f, r.width - 2 * kSliderKnob);
  const float t = std::clamp((x - r.x - kSliderKnob) / usable, 0.0f, 1.0f);
  change(options_.min + t * (options_.max - options_.min));
}

Size Slider::measure(Size) { return {kSliderMinWidth, kSliderHeight}; }

void Slider::paint(Painter& p) {
  const Theme& th = theme();
  const Rect r = bounds();
  const bool off = !enabled();
  const float hover = at(hover_);
  const float usable = std::max(0.0f, r.width - 2 * kSliderKnob);
  const float cy = r.y + r.height * 0.5f;
  const float kx = r.x + kSliderKnob + usable * fraction();
  const Rect track{r.x + kSliderKnob, cy - kTrackThickness * 0.5f, usable, kTrackThickness};
  p.fill_rounded_rect(track, kTrackThickness * 0.5f, th.selected);
  p.fill_rounded_rect({track.x, track.y, kx - track.x, track.height}, kTrackThickness * 0.5f,
                      detail::dim_if(off, th.accent));
  const float radius = kSliderKnob + kKnobHoverGrowth * hover;
  if (th.shadows) {
    const Rect knob_box{kx - radius, cy - radius, 2 * radius, 2 * radius};
    p.shadow({knob_box.x, knob_box.y + kKnobShadowOffset, knob_box.width, knob_box.height}, radius,
             kKnobShadowBlur, th.shadow, knob_box);
  }
  p.fill_circle({kx, cy}, radius, detail::dim_if(off, Color::hex(0xFFFFFF)));
  p.stroke_circle({kx, cy}, radius, 1, th.border);
  if (focus_visible())
    detail::focus_ring(p, {kx - radius, cy - radius, 2 * radius, 2 * radius}, radius, th);
}

bool Slider::on_mouse(const MouseEvent& e) {
  if (e.kind == MouseEvent::Kind::press && e.button == MouseButton::left) {
    set_from_pointer(e.position.x);
    return true;
  }
  if (e.kind == MouseEvent::Kind::move && pressed()) {
    set_from_pointer(e.position.x);
    return true;
  }
  return e.kind == MouseEvent::Kind::release;
}

bool Slider::on_key(const KeyEvent& e) {
  if (!e.down) return false;
  const float range = options_.max - options_.min;
  const float step = options_.step > 0 ? options_.step : range * kKeyStepFraction;
  switch (e.key) {
    case Key::left:
    case Key::down:
      change(value_ - step);
      return true;
    case Key::right:
    case Key::up:
      change(value_ + step);
      return true;
    case Key::page_down:
      change(value_ - std::max(step, range * kPageStepFraction));
      return true;
    case Key::page_up:
      change(value_ + std::max(step, range * kPageStepFraction));
      return true;
    case Key::home:
      change(options_.min);
      return true;
    case Key::end:
      change(options_.max);
      return true;
    default:
      return false;
  }
}

void Slider::on_hover_changed(bool hovered) {
  animate(hover_, hovered ? 1.0f : 0.0f, theme().motion_fast);
}

// At either end of the track the knob reaches past the bounds; its shadow and focus ring reach
// further. One more pixel covers anti-aliasing.
float Slider::overflow() const {
  const float shadow = kKnobShadowOffset + kShadowReachBlurs * kKnobShadowBlur;
  const float ring = detail::kFocusRingGap + detail::kFocusRingWidth;
  return kKnobHoverGrowth + std::max(shadow, ring) + 1;
}

// --- ProgressBar --------------------------------------------------------------------------

ProgressBar::ProgressBar(float value) : value_(std::clamp(value, 0.0f, 1.0f)) { fill_.to = value_; }

void ProgressBar::set_value(float v) {
  v = std::clamp(v, 0.0f, 1.0f);
  if (v == value_) return;
  value_ = v;
  animate(fill_, v, theme().motion_base);
}

void ProgressBar::set_indeterminate(bool indeterminate) {
  if (indeterminate == indeterminate_) return;
  indeterminate_ = indeterminate;
  if (indeterminate) keep_animating();
  invalidate();
}

Size ProgressBar::measure(Size) { return {kProgressMinWidth, kProgressThickness}; }

void ProgressBar::paint(Painter& p) {
  const Theme& th = theme();
  const Rect r = bounds();
  const Rect track{r.x, r.y + (r.height - kProgressThickness) * 0.5f, r.width, kProgressThickness};
  const float radius = kProgressThickness * 0.5f;
  p.fill_rounded_rect(track, radius, th.selected);
  if (!indeterminate_) {
    const float t = at(fill_);
    if (t > 0)
      p.fill_rounded_rect({track.x, track.y, track.width * t, track.height}, radius, th.accent);
    return;
  }
  // A segment that sweeps across and wraps; its position follows the frame clock directly.
  const float phase =
      static_cast<float>(frame_time_ns() % kSweepPeriodNs) / static_cast<float>(kSweepPeriodNs);
  const float seg = track.width * kSweepWidth;
  const float x = track.x - seg + (track.width + seg) * phase;
  // Cut to the track, keeping round ends where the segment meets the track's own ends.
  const float x0 = std::max(x, track.x), x1 = std::min(x + seg, track.right());
  if (x1 > x0)
    p.fill_rounded_rect({x0, track.y, x1 - x0, track.height}, std::min(radius, (x1 - x0) * 0.5f),
                        th.accent);
  keep_animating();  // one more frame, for as long as the bar is drawn
}

// --- Separator ----------------------------------------------------------------------------

Size Separator::measure(Size) {
  return orientation_ == Orientation::horizontal ? Size{0, 1} : Size{1, 0};
}

void Separator::paint(Painter& p) {
  const Rect r = bounds();
  const float h = p.hairline();
  if (orientation_ == Orientation::horizontal)
    p.fill_rect({r.x, r.y + (r.height - h) * 0.5f, r.width, h}, theme().divider);
  else
    p.fill_rect({r.x + (r.width - h) * 0.5f, r.y, h, r.height}, theme().divider);
}

}  // namespace smalti
