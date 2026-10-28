#include "smalti/widget.hpp"

#include <algorithm>
#include <cmath>

#include "core/state.hpp"
#include "smalti/app.hpp"
#include "smalti/window.hpp"

namespace smalti {

namespace {

// A widget built before it was added (a composite that adds children in its constructor) has
// descendants that do not know their window yet; they learn it with their root.
void attach(Widget& w, Window* window) {
  detail::Tree::set_window(w, window);
  for (auto& child : w.children()) attach(*child, window);
}

float ease(Easing e, float t) {
  switch (e) {
    case Easing::linear: return t;
    case Easing::ease_out: return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);  // cubic
    case Easing::ease_in_out: return t < 0.5f ? 4 * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
  }
  return t;
}

}  // namespace

float Motion::at(int64_t now_ns) const {
  if (duration_ns <= 0 || now_ns >= start_ns + duration_ns) return to;
  if (now_ns <= start_ns) return from;
  const float t = static_cast<float>(now_ns - start_ns) / static_cast<float>(duration_ns);
  return from + (to - from) * ease(easing, t);
}

Widget::Widget() : id_(detail::Registry::instance().allocate(this)) {}

Widget::~Widget() {
  // Children go first (the vector destroys them), then this slot; Refs to any of them go stale.
  children_.clear();
  detail::Registry::instance().release(id_);
}

Ref<Widget> Widget::parent() const { return parent_ ? Ref<Widget>::from_id(parent_->id()) : Ref<Widget>{}; }

Ref<Window> Widget::window() const { return window_ ? Ref<Window>::from_id(window_->id()) : Ref<Window>{}; }

Ref<Widget> Widget::child(size_t index) const {
  return index < children_.size() ? Ref<Widget>::from_id(children_[index]->id()) : Ref<Widget>{};
}

void Widget::adopt(std::unique_ptr<Widget> child) {
  if (!accepts_children()) {
    detail::contract_violation("add<T>() was called on a widget that takes no children; add to a Column, Row, "
                               "Grid, Card or Window instead");
  }
  if (const auto* ws = detail::window_state_of(*this); ws && ws->in_draw) {
    detail::contract_violation(
        "add<T>() was called while the window was laying out or painting (from measure, "
        "arrange or paint); add widgets from an event handler or App::post instead");
  }
  child->parent_ = this;
  attach(*child, window_);
  children_.push_back(std::move(child));
  invalidate_layout();
}

void Widget::remove() {
  if (removing_) return;
  removing_ = true;
  detail::app_state().schedule_removal(id_);
}

void Widget::set_stretch(float factor) {
  stretch_ = std::max(0.0f, factor);
  invalidate_layout();
}

void Widget::set_align(Align align) {
  align_ = align;
  align_set_ = true;
  invalidate_layout();
}

void Widget::set_min_size(Size size) {
  min_size_ = size;
  invalidate_layout();
}

void Widget::set_column_span(int columns) {
  column_span_ = std::max(1, columns);
  invalidate_layout();
}

void Widget::set_visible(bool visible) {
  if (visible_ == visible) return;
  const detail::DispatchScope scope(detail::app_state());  // the hooks below must not free this
  visible_ = visible;
  if (!visible) {
    if (auto* ws = detail::window_state_of(*this)) ws->release_subtree(*this);
  }
  invalidate_layout();
}

void Widget::set_enabled(bool enabled) {
  if (enabled_ == enabled) return;
  const detail::DispatchScope scope(detail::app_state());  // the hooks below must not free this
  enabled_ = enabled;
  if (!enabled) {
    if (auto* ws = detail::window_state_of(*this)) ws->release_subtree(*this);
  }
  invalidate();
}

bool Widget::enabled() const {
  for (const Widget* w = this; w; w = w->parent_) {
    if (!w->enabled_) return false;
  }
  return true;
}

void Widget::focus() {
  auto* ws = detail::window_state_of(*this);
  if (!ws || !focusable() || !detail::eligible(*this)) return;
  const detail::DispatchScope scope(detail::app_state());
  ws->focus(this);
}

bool Widget::has_focus() const {
  const auto* ws = detail::window_state_of(*this);
  return ws && ws->focused == id_;
}

Size Widget::measure(Size) { return min_size_; }

void Widget::arrange(Rect bounds) {
  if (bounds_ != bounds) invalidate();  // the old place
  bounds_ = bounds;
  invalidate();                         // the new place
}

void Widget::paint(Painter&) {}
bool Widget::on_mouse(const MouseEvent&) { return false; }
bool Widget::on_key(const KeyEvent&) { return false; }
void Widget::on_focus_changed(bool) { invalidate(); }
void Widget::on_hover_changed(bool) {}

void Widget::invalidate() {
  if (auto* ws = detail::window_state_of(*this)) {
    const float o = overflow();
    ws->invalidate({bounds_.x - o, bounds_.y - o, bounds_.width + 2 * o, bounds_.height + 2 * o});
  }
}

void Widget::invalidate_layout() {
  if (auto* ws = detail::window_state_of(*this)) ws->invalidate_layout();
}

const Theme& Widget::theme() const { return detail::app_state().theme; }

bool Widget::hovered() const {
  const auto* ws = detail::window_state_of(*this);
  return ws && ws->hovered == id_;
}

bool Widget::pressed() const {
  const auto* ws = detail::window_state_of(*this);
  return ws && ws->pressed == id_;
}

bool Widget::focus_visible() const {
  const auto* ws = detail::window_state_of(*this);
  return ws && ws->focused == id_ && ws->keyboard_mode && ws->window_focused;
}

void Widget::animate(Motion& m, float target, std::chrono::milliseconds duration, Easing easing) {
  auto* ws = detail::window_state_of(*this);
  const int64_t now = ws ? ws->now() : 0;
  m.from = m.at(now);
  m.to = target;
  m.start_ns = now;
  m.easing = easing;
  const bool instant = !ws || detail::app_state().system.reduced_motion;
  m.duration_ns = instant ? 0 : std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
  if (ws && m.duration_ns > 0) ws->animate_until(id_, m.start_ns + m.duration_ns);
  invalidate();
}

void Widget::keep_animating() {
  if (auto* ws = detail::window_state_of(*this)) ws->animate_until(id_, ws->now() + 1);
}

int64_t Widget::frame_time_ns() const {
  const auto* ws = detail::window_state_of(*this);
  return ws ? ws->frame_time : 0;
}

float Widget::at(const Motion& m) const {
  const auto* ws = detail::window_state_of(*this);
  return m.at(ws ? ws->frame_time : 0);
}

}  // namespace smalti
