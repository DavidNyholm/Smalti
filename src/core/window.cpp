// Window, and the per-window pipeline: input dispatch, focus, damage, layout, paint, present.
#include "smalti/window.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/state.hpp"
#include "smalti/app.hpp"

namespace smalti {

namespace detail {

namespace {

// Beyond this many separate damaged rectangles, one bounding rectangle is cheaper to manage.
constexpr size_t kMaxDirtyRects = 8;

RectI to_device(const Rect& r, float scale) {
  const int x0 = static_cast<int>(std::floor(r.x * scale)), y0 = static_cast<int>(std::floor(r.y * scale));
  const int x1 = static_cast<int>(std::ceil(r.right() * scale)), y1 = static_cast<int>(std::ceil(r.bottom() * scale));
  return {x0, y0, x1 - x0, y1 - y0};
}

bool overlaps(const RectI& a, const RectI& b) {
  return a.x <= b.right() && b.x <= a.right() && a.y <= b.bottom() && b.y <= a.bottom();
}

Widget* find(WidgetId id) { return Registry::instance().find(id); }

void paint_tree(Widget& w, Painter& p, const RectI& clip, float scale) {
  if (!w.visible()) return;
  const float o = w.overflow();
  const Rect b = w.bounds();
  if (!overlaps(to_device({b.x - o, b.y - o, b.width + 2 * o, b.height + 2 * o}, scale), clip)) return;
  w.paint(p);
  for (auto& child : w.children()) paint_tree(*child, p, clip, scale);
}

Widget* hit(Widget& w, Point p) {
  if (!w.visible() || !w.bounds().contains(p)) return nullptr;
  auto& kids = w.children();
  for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
    if (Widget* h = hit(**it, p)) return h;
  }
  return &w;
}

void collect_focusable(Widget& w, std::vector<Widget*>& out) {
  if (!w.visible()) return;
  if (w.tab_stop() && w.enabled()) out.push_back(&w);
  for (auto& child : w.children()) collect_focusable(*child, out);
}

}  // namespace

std::unique_ptr<Widget> Tree::detach(Widget& w) {
  Widget* parent = w.parent_;
  if (!parent) return nullptr;
  auto& kids = parent->children_;
  const auto it = std::find_if(kids.begin(), kids.end(), [&](const auto& c) { return c.get() == &w; });
  if (it == kids.end()) return nullptr;
  std::unique_ptr<Widget> owned = std::move(*it);
  kids.erase(it);
  owned->parent_ = nullptr;
  parent->invalidate_layout();
  return owned;
}

WindowState* window_state_of(const Widget& w) {
  const Window* win = Tree::window_of(w);
  return win ? Tree::state_of(*win) : nullptr;
}

WindowState::WindowState(App& app_ref, Window& window_ref, const WindowOptions& options)
    : app(app_ref), window(window_ref), title(options.title), min_size(options.min_size) {
  AppState& as = app.state();
  paint.canvas = &canvas;
  paint.glyphs = &as.glyphs;
  paint.fonts = &as.fonts;
  paint.theme = &as.theme;
  // The backend reports the size and scale through on_resize while it creates the window,
  // before `native` is set here; the frame buffer is fetched once it is.
  native = as.backend->create_window(options, *this);
  frame = native->frame_buffer();
  canvas.set_target(frame);
  native->set_dark_frame(as.theme.dark);
}

WindowState::~WindowState() = default;

int64_t WindowState::now() const { return app.state().backend->now_ns(); }

void WindowState::request_frame() {
  if (native && !native->frame_requested()) native->request_frame();
}

void WindowState::invalidate(const Rect& logical) {
  RectI r = to_device(logical, scale).intersect({0, 0, width_px, height_px});
  if (r.empty()) return;
  // Merge into an overlapping rectangle (repeatedly, as a merge can create new overlaps).
  for (bool merged = true; merged;) {
    merged = false;
    for (auto it = dirty.begin(); it != dirty.end(); ++it) {
      if (overlaps(*it, r)) {
        r = r.unite(*it);
        dirty.erase(it);
        merged = true;
        break;
      }
    }
  }
  dirty.push_back(r);
  if (dirty.size() > kMaxDirtyRects) {
    RectI all = dirty.front();
    for (const RectI& d : dirty) all = all.unite(d);
    dirty.assign(1, all);
  }
  request_frame();
}

void WindowState::invalidate_all() {
  dirty.assign(1, RectI{0, 0, width_px, height_px});
  request_frame();
}

void WindowState::invalidate_layout() {
  layout_dirty = true;
  invalidate_all();
}

void WindowState::animate_until(WidgetId id, int64_t end_ns) {
  for (auto& [wid, until] : animating) {
    if (wid == id) {
      until = std::max(until, end_ns);
      request_frame();
      return;
    }
  }
  animating.emplace_back(id, end_ns);
  request_frame();
}

void WindowState::draw(int64_t frame_time_ns) {
  frame_time = frame_time_ns;
  // Every animating widget is drawn on every frame until its animation has ended; the frame at
  // or after the end draws its final state, and then it stops costing anything.
  for (auto it = animating.begin(); it != animating.end();) {
    Widget* w = find(it->first);
    if (w && w->visible()) w->invalidate();
    if (!w || !w->visible() || frame_time >= it->second) it = animating.erase(it);
    else ++it;
  }
  if (layout_dirty) {
    layout_dirty = false;
    window.arrange({0, 0, static_cast<float>(width_px) / scale, static_cast<float>(height_px) / scale});
    dirty.assign(1, RectI{0, 0, width_px, height_px});
  }
  if (dirty.empty() || frame.empty()) return;
  paint.scale = scale;
  Painter painter(paint);
  for (const RectI& r : dirty) {
    canvas.set_clip(r);
    paint.clips.assign(1, canvas.clip());
    paint_tree(window, painter, r, scale);
  }
  native->present(dirty);
  dirty.clear();
  ++frames_drawn;
  if (!animating.empty()) request_frame();
}

Widget* WindowState::hit_test(Point logical) const { return hit(window, logical); }

void WindowState::set_hovered(Widget* w) {
  const WidgetId id = w ? w->id() : WidgetId{};
  if (id == hovered) return;
  Widget* old = find(hovered);
  hovered = id;
  if (old) old->on_hover_changed(false);
  if (w) w->on_hover_changed(true);
  if (native) native->set_cursor(w && w->enabled() ? w->cursor() : Cursor::arrow);
}

void WindowState::focus(Widget* w) {
  const WidgetId id = w ? w->id() : WidgetId{};
  if (id == focused) return;
  Widget* old = find(focused);
  focused = id;
  if (old) old->on_focus_changed(false);
  if (w) w->on_focus_changed(true);
}

void WindowState::move_focus(bool backwards) {
  std::vector<Widget*> order;
  collect_focusable(window, order);
  if (order.empty()) return;
  const auto it = std::find(order.begin(), order.end(), find(focused));
  size_t next;
  if (it == order.end()) next = backwards ? order.size() - 1 : 0;
  else {
    const size_t i = static_cast<size_t>(it - order.begin());
    next = backwards ? (i + order.size() - 1) % order.size() : (i + 1) % order.size();
  }
  focus(order[next]);
  order[next]->invalidate();
}

void WindowState::on_mouse_move(Point device, Modifiers mods) {
  app.state().guarded([&] {
    const Point p{device.x / scale, device.y / scale};
    last_mouse = p;
    MouseEvent e{MouseEvent::Kind::move, p, MouseButton::none, mods, {}, 1};
    if (Widget* captured = find(pressed)) {
      captured->on_mouse(e);
    } else if (Widget* h = hit_test(p)) {
      if (h->enabled()) h->on_mouse(e);
    }
    set_hovered(hit_test(p));
  });
}

void WindowState::on_mouse_button(MouseButton button, bool down, Point device, Modifiers mods, int clicks) {
  app.state().guarded([&] {
    const Point p{device.x / scale, device.y / scale};
    last_mouse = p;
    MouseEvent e{down ? MouseEvent::Kind::press : MouseEvent::Kind::release, p, button, mods, {}, clicks};
    if (!down) {
      Widget* captured = find(pressed);
      pressed = {};
      native->capture_mouse(false);
      if (captured) {
        captured->on_mouse(e);
        captured->invalidate();
      }
      set_hovered(hit_test(p));
      return;
    }
    if (keyboard_mode) {
      keyboard_mode = false;  // the pointer is in use: hide the focus ring
      if (Widget* f = find(focused)) f->invalidate();
    }
    Widget* target = hit_test(p);
    Widget* focus_target = target;
    while (focus_target && !(focus_target->focusable() && focus_target->enabled())) {
      focus_target = Tree::parent_of(*focus_target);
    }
    focus(focus_target);
    for (Widget* w = target; w; w = Tree::parent_of(*w)) {
      if (w->enabled() && w->on_mouse(e)) {
        pressed = w->id();
        native->capture_mouse(true);
        w->invalidate();
        break;
      }
    }
  });
}

void WindowState::on_mouse_wheel(Point device, Point delta, Modifiers mods) {
  app.state().guarded([&] {
    const Point p{device.x / scale, device.y / scale};
    const MouseEvent e{MouseEvent::Kind::wheel, p, MouseButton::none, mods, delta, 1};
    for (Widget* w = hit_test(p); w; w = Tree::parent_of(*w)) {
      if (w->enabled() && w->on_mouse(e)) break;
    }
  });
}

void WindowState::on_mouse_leave() {
  app.state().guarded([&] {
    if (!find(pressed)) set_hovered(nullptr);
  });
}

void WindowState::on_key(const KeyEvent& e) {
  app.state().guarded([&] {
    if (e.down && e.key == Key::tab && !e.modifiers.ctrl && !e.modifiers.alt) {
      keyboard_mode = true;
      move_focus(e.modifiers.shift);
      return;
    }
    const bool navigation = e.key == Key::left || e.key == Key::right || e.key == Key::up || e.key == Key::down ||
                            e.key == Key::space || e.key == Key::enter;
    if (e.down && navigation && !keyboard_mode) {
      keyboard_mode = true;
      if (Widget* f = find(focused)) f->invalidate();
    }
    for (Widget* w = find(focused); w; w = Tree::parent_of(*w)) {
      if (w->enabled() && w->on_key(e)) break;
    }
  });
}

void WindowState::on_focus(bool gained) {
  window_focused = gained;
  if (Widget* f = find(focused)) f->invalidate();
}

void WindowState::on_resize(int w, int h, float new_scale) {
  width_px = std::max(0, w);
  height_px = std::max(0, h);
  scale = new_scale > 0 ? new_scale : 1.0f;
  frame = native ? native->frame_buffer() : PixelBuffer{};
  canvas.set_target(frame);
  layout_dirty = true;
  invalidate_all();
}

void WindowState::on_frame(int64_t frame_time_ns) {
  app.state().guarded([&] { draw(frame_time_ns); });
}

void WindowState::on_close_request() {
  app.state().guarded([&] { window.close(); });
}

}  // namespace detail

Window::Window(App& app, const WindowOptions& options) {
  detail::Tree::set_window(*this, this);
  state_ = std::make_unique<detail::WindowState>(app, *this, options);
}

Window::~Window() {
  // Children first, while the window's state still exists for anything they touch on the way.
  children_.clear();
  state_.reset();
}

void Window::set_title(std::string_view title) {
  state_->title.assign(title);
  state_->native->set_title(title);
}

const std::string& Window::title() const { return state_->title; }

void Window::close() { remove(); }

float Window::scale() const { return state_->scale; }

Size Window::client_size() const {
  return {static_cast<float>(state_->width_px) / state_->scale, static_cast<float>(state_->height_px) / state_->scale};
}

Size Window::measure(Size available) { return available; }

void Window::arrange(Rect bounds) {
  detail::Tree::set_bounds(*this, bounds);
  for (auto& child : children()) {
    if (child->visible()) child->arrange(bounds);
  }
}

void Window::paint(Painter& p) { p.fill_rect(bounds(), theme().background); }

}  // namespace smalti
