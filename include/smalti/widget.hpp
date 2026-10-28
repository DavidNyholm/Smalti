// Widget: the base of everything in a window's tree, including the window itself.
//
// Parents own their children. add<T>() creates a child and returns a Ref to it; remove() takes
// a widget (and its children) out at the end of the current event, so a callback may remove the
// very widget that called it. Custom widgets override measure/arrange/paint and the input hooks.
#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "smalti/geometry.hpp"
#include "smalti/input.hpp"
#include "smalti/painter.hpp"
#include "smalti/ref.hpp"
#include "smalti/theme.hpp"

namespace smalti {

class Window;
namespace detail {
class Tree;
}

// Where a child sits across its parent's layout direction (and, for justify, along it).
enum class Align : uint8_t { start, center, end, stretch };

enum class Easing : uint8_t { linear, ease_out, ease_in_out };

// An animated value. It is a pure function of the window's frame clock, so it costs nothing
// between animations and is exactly where it should be on every frame, whatever the frame rate.
struct Motion {
  float from = 0, to = 0;
  int64_t start_ns = 0, duration_ns = 0;
  Easing easing = Easing::ease_out;
  float at(int64_t now_ns) const;
};

class Widget {
 public:
  Widget();
  virtual ~Widget();
  Widget(const Widget&) = delete;
  Widget& operator=(const Widget&) = delete;

  WidgetId id() const { return id_; }
  Ref<Widget> parent() const;
  Ref<Window> window() const;

  // Creates a child of type T. Only containers take children (Column, Row, Grid, Card, Window);
  // adding to anything else is a contract violation.
  template <class T, class... Args>
  Ref<T> add(Args&&... args) {
    auto child = std::make_unique<T>(std::forward<Args>(args)...);
    const WidgetId id = child->id();
    adopt(std::move(child));
    return Ref<T>::from_id(id);
  }
  // Removes this widget and its children once the current event has been handled.
  void remove();

  size_t child_count() const { return children_.size(); }
  Ref<Widget> child(size_t index) const;
  // The children in order, for layouts and painting. Read-only: add<T>() and remove() change it.
  const std::vector<std::unique_ptr<Widget>>& children() const { return children_; }

  // Layout parameters, read by the parent's layout.
  void set_stretch(float factor);  // share of leftover space along the parent's direction
  float stretch() const { return stretch_; }
  void set_align(Align align);  // placement across the parent's direction
  // The child's own alignment, or `fallback` (the container's default) if none was set.
  Align align_or(Align fallback) const { return align_set_ ? align_ : fallback; }
  void set_min_size(Size size);
  Size min_size() const { return min_size_; }
  void set_column_span(int columns);  // in a Grid
  int column_span() const { return column_span_; }

  void set_visible(bool visible);
  bool visible() const { return visible_; }
  void set_enabled(bool enabled);
  bool enabled() const;  // false if this widget or any ancestor is disabled

  Rect bounds() const { return bounds_; }  // window coordinates, logical pixels

  // Takes keyboard focus, if the widget accepts it and the window is open.
  void focus();
  bool has_focus() const;

  // The size the widget wants within `available` (either may be unbounded: a huge value).
  virtual Size measure(Size available);
  // Places the widget; containers place their children here.
  virtual void arrange(Rect bounds);
  virtual void paint(Painter& p);

  virtual bool on_mouse(const MouseEvent& e);  // true if handled
  virtual bool on_key(const KeyEvent& e);      // true if handled
  virtual void on_focus_changed(bool gained);
  virtual void on_hover_changed(bool hovered);
  virtual bool focusable() const { return false; }
  // Whether Tab stops here. Usually the same as focusable(); a radio group has one stop.
  virtual bool tab_stop() const { return focusable(); }
  virtual bool accepts_children() const { return false; }
  virtual Cursor cursor() const { return Cursor::arrow; }
  // How far paint() may draw beyond the bounds (shadows, focus rings), in logical pixels.
  virtual float overflow() const { return 4; }

  void invalidate();         // paint again
  void invalidate_layout();  // measure and arrange again, then paint

 protected:
  const Theme& theme() const;

  bool hovered() const;
  bool pressed() const;        // the mouse went down on this widget and has not been released
  bool focus_visible() const;  // focused, and the user is navigating with the keyboard

  // Starts animating `m` from its current value to `target`. Under reduced motion, or with a
  // zero duration, it jumps.
  void animate(Motion& m, float target, std::chrono::milliseconds duration,
               Easing easing = Easing::ease_out);
  float at(const Motion& m) const;  // the motion's value at the current frame
  // Asks for one more frame after this one, for animations with no end (indeterminate progress).
  void keep_animating();
  int64_t frame_time_ns() const;

 private:
  friend class Window;
  friend class detail::Tree;
  void adopt(std::unique_ptr<Widget> child);

  WidgetId id_;
  Widget* parent_ = nullptr;
  Window* window_ = nullptr;
  std::vector<std::unique_ptr<Widget>> children_;
  Rect bounds_;
  Size min_size_;
  float stretch_ = 0;
  int column_span_ = 1;
  Align align_ = Align::stretch;
  bool align_set_ = false;
  bool visible_ = true;
  bool enabled_ = true;
  bool removing_ = false;
};

}  // namespace smalti
