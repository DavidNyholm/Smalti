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
  return rect_from_edges(std::floor(double{r.x} * scale), std::floor(double{r.y} * scale),
                         std::ceil(double{r.right()} * scale),
                         std::ceil(double{r.bottom()} * scale));
}

bool overlaps(const RectI& a, const RectI& b) {
  return a.x <= b.right() && b.x <= a.right() && a.y <= b.bottom() && b.y <= a.bottom();
}

Widget* find(WidgetId id) { return Registry::instance().find(id); }

// The default minimum must not override a smaller size asked for explicitly.
WindowOptions normalized(const WindowOptions& o) {
  WindowOptions n = o;
  n.min_size = {std::min(o.min_size.width, o.size.width),
                std::min(o.min_size.height, o.size.height)};
  return n;
}

void paint_tree(Widget& w, Painter& p, const RectI& clip, float scale) {
  if (!w.visible()) return;
  const float o = w.overflow();
  const Rect b = w.bounds();
  // Missing the clip skips only this widget's own painting: a child may paint further out than
  // its parent does (a card's shadow, inside a row), so the children are still visited.
  if (overlaps(to_device({b.x - o, b.y - o, b.width + 2 * o, b.height + 2 * o}, scale), clip))
    w.paint(p);
  for (size_t i = 0; i < w.children().size(); ++i) paint_tree(*w.children()[i], p, clip, scale);
}

Widget* hit(Widget& w, Point p) {
  if (!w.visible() || !w.bounds().contains(p)) return nullptr;
  auto& kids = w.children();
  for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
    if (Widget* h = hit(**it, p)) return h;
  }
  return &w;
}

bool within(const Widget& root, const Widget* w) {
  for (; w; w = Tree::parent_of(*w)) {
    if (w == &root) return true;
  }
  return false;
}

void collect_focusable(Widget& w, std::vector<Widget*>& out) {
  if (!w.visible()) return;
  if (w.tab_stop() && w.enabled()) out.push_back(&w);
  for (auto& child : w.children()) collect_focusable(*child, out);
}

}  // namespace

bool shown(const Widget& w) {
  for (const Widget* x = &w; x; x = Tree::parent_of(*x)) {
    if (!x->visible()) return false;
  }
  return true;
}

bool eligible(const Widget& w) { return w.enabled() && shown(w); }

std::unique_ptr<Widget> Tree::detach(Widget& w) {
  Widget* parent = w.parent_;
  if (!parent) return nullptr;
  auto& kids = parent->children_;
  const auto it =
      std::find_if(kids.begin(), kids.end(), [&](const auto& c) { return c.get() == &w; });
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

WindowState::WindowState(App& app_ref, Window& window_ref, const WindowOptions& requested)
    : app(app_ref),
      window(window_ref),
      title(requested.title),
      min_size(normalized(requested).min_size) {
  const WindowOptions options = normalized(requested);
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
  invalidate_layout();  // the first frame: on_resize's request came before there was a `native`
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

void WindowState::redraw_at(WidgetId id, int64_t time_ns) {
  for (auto& [wid, when] : timed) {
    if (wid == id) {
      when = std::min(when, time_ns);  // the earlier stays; the widget asks again when it draws
      return;
    }
  }
  timed.emplace_back(id, time_ns);
}

int64_t WindowState::next_redraw() const {
  int64_t next = INT64_MAX;
  for (const auto& [id, when] : timed) next = std::min(next, when);
  return next;
}

void WindowState::run_redraws(int64_t now) {
  for (auto it = timed.begin(); it != timed.end();) {
    if (it->second > now) {
      ++it;
      continue;
    }
    // A widget removed or hidden since it asked has nothing to redraw.
    if (Widget* w = find(it->first); w && w->visible()) w->invalidate();
    it = timed.erase(it);
  }
}

void WindowState::draw(int64_t frame_time_ns) {
  // A paint() that runs a nested event loop (a native message box) must not get a frame of this
  // window drawn inside the one it is part of: that one waits until this frame is done.
  if (in_draw) {
    request_frame();
    return;
  }
  in_draw = true;
  try {
    draw_frame(frame_time_ns);
  } catch (...) {
    in_draw = false;
    drawing.clear();
    invalidate_layout();  // the frame was cut short: lay out and draw all of it again
    throw;
  }
  in_draw = false;
}

void WindowState::draw_frame(int64_t frame_time_ns) {
  frame_time = frame_time_ns;
  // Every animating widget is drawn on every frame until its animation has ended; the frame at
  // or after the end draws its final state, and then it stops costing anything.
  for (auto it = animating.begin(); it != animating.end();) {
    Widget* w = find(it->first);
    if (w && w->visible()) w->invalidate();
    if (!w || !w->visible() || frame_time >= it->second)
      it = animating.erase(it);
    else
      ++it;
  }
  if (layout_dirty) {
    layout_dirty = false;
    window.arrange(
        {0, 0, static_cast<float>(width_px) / scale, static_cast<float>(height_px) / scale});
    dirty.assign(1, RectI{0, 0, width_px, height_px});
  }
  if (dirty.empty() || frame.empty()) return;
  // Paint from a batch of its own: a widget that invalidates while painting adds to `dirty` for
  // the next frame instead of changing the list being drawn.
  drawing.swap(dirty);
  dirty.clear();
  paint.scale = scale;
  Painter painter(paint);
  for (const RectI& r : drawing) {
    canvas.set_clip(r);
    paint.clips.assign(1, canvas.clip());
    paint_tree(window, painter, r, scale);
  }
  native->present(drawing);
  drawing.clear();
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
  // The old widget's hook may have moved the hover on, or hidden w: then w never had it, and
  // the nested call has already set the cursor.
  if (hovered != id) return;
  if (w) w->on_hover_changed(true);
  if (hovered != id) return;  // its own hook moved the hover on (or hid it): the cursor is set
  if (native) native->set_cursor(w && w->enabled() ? w->cursor() : Cursor::arrow);
}

void WindowState::focus(Widget* w) {
  const WidgetId id = w ? w->id() : WidgetId{};
  if (id == focused) return;
  Widget* old = find(focused);
  focused = id;
  if (old) old->on_focus_changed(false);
  // The old widget's hook may have moved focus on, or hidden w: then w never had it.
  if (w && focused == id) w->on_focus_changed(true);
}

void WindowState::cancel_press() {
  if (pressed == WidgetId{}) return;
  Widget* w = find(pressed);
  pressed = {};  // first: releasing the capture can report the loss straight back
  ++press_serial;
  if (native) native->capture_mouse(false);
  if (w) {
    w->on_mouse({MouseEvent::Kind::cancel, last_mouse, MouseButton::none, {}, {}, 1});
    w->invalidate();
  }
}

void WindowState::release_subtree(const Widget& root) {
  if (within(root, find(pressed))) cancel_press();
  if (within(root, find(focused))) {
    if (Widget* f = find(focused)) f->invalidate();
    focus(nullptr);
  }
  if (within(root, find(hovered))) set_hovered(nullptr);
}

void WindowState::move_focus(bool backwards) {
  std::vector<Widget*> order;
  collect_focusable(window, order);
  if (order.empty()) return;
  const auto it = std::find(order.begin(), order.end(), find(focused));
  size_t next;
  if (it == order.end())
    next = backwards ? order.size() - 1 : 0;
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
      if (eligible(*captured))
        captured->on_mouse(e);
      else
        cancel_press();
    } else if (Widget* h = hit_test(p)) {
      if (h->enabled()) h->on_mouse(e);
    }
    set_hovered(hit_test(p));
  });
}

void WindowState::on_mouse_button(MouseButton button, bool down, Point device, Modifiers mods,
                                  int clicks) {
  app.state().guarded([&] {
    const Point p{device.x / scale, device.y / scale};
    last_mouse = p;
    MouseEvent e{
        down ? MouseEvent::Kind::press : MouseEvent::Kind::release, p, button, mods, {}, clicks};
    const bool pressing = pressed != WidgetId{};
    if (!down) {
      // A press still being set up (a focus hook ran a nested loop) ends here, before any widget
      // took it.
      if (press_pending && !pressing && button == pressed_button) {
        press_pending = false;
        ++press_serial;
        set_hovered(hit_test(p));
        return;
      }
      if (!pressing || button != pressed_button) {
        set_hovered(hit_test(p));  // only the button that started the press ends it
        return;
      }
      Widget* captured = find(pressed);
      if (captured && !eligible(*captured)) {
        cancel_press();
        captured = nullptr;
      }
      pressed = {};
      ++press_serial;
      native->capture_mouse(false);
      if (captured) {
        captured->on_mouse(e);
        captured->invalidate();
      }
      set_hovered(hit_test(p));
      return;
    }
    if (pressing || press_pending) return;  // one press at a time: another button waits
    if (keyboard_mode) {
      keyboard_mode = false;  // the pointer is in use: hide the focus ring
      if (Widget* f = find(focused)) f->invalidate();
    }
    Widget* target = hit_test(p);
    Widget* focus_target = target;
    while (focus_target && !(focus_target->focusable() && focus_target->enabled())) {
      focus_target = Tree::parent_of(*focus_target);
    }
    // The press is under way from here: a release of this button pumped by a focus hook's nested
    // loop ends it before any widget takes it.
    press_pending = true;
    pressed_button = button;
    struct EndPending {
      bool& flag;
      ~EndPending() { flag = false; }
    } end_pending{press_pending};
    const uint64_t interaction = press_serial;
    focus(focus_target);
    if (press_serial != interaction) return;  // released already
    for (Widget* w = target; w; w = Tree::parent_of(*w)) {
      if (!eligible(*w)) continue;
      // The press is this widget's while its handler runs, so hiding, disabling or removing it
      // from there (or a release pumped by a nested loop) ends this press and no later one.
      pressed = w->id();
      pressed_button = button;
      const bool taken = w->on_mouse(e);
      if (press_serial != interaction) break;  // nested dispatch ended it: it goes no further
      if (!taken) {
        pressed = {};
        continue;
      }
      native->capture_mouse(true);
      w->invalidate();
      if (!eligible(*w)) cancel_press();
      break;
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
    const bool navigation = e.key == Key::left || e.key == Key::right || e.key == Key::up ||
                            e.key == Key::down || e.key == Key::space || e.key == Key::enter;
    if (e.down && navigation && !keyboard_mode) {
      keyboard_mode = true;
      if (Widget* f = find(focused)) f->invalidate();
    }
    Widget* f = find(focused);
    if (f && !eligible(*f)) {
      focus(nullptr);  // hidden or disabled since it took focus: keys must not reach it
      f = nullptr;
    }
    for (Widget* w = f; w; w = Tree::parent_of(*w)) {
      if (w->enabled() && w->on_key(e)) break;
    }
  });
}

void WindowState::on_focus(bool gained) {
  window_focused = gained;
  if (Widget* f = find(focused)) f->invalidate();
}

void WindowState::on_capture_lost() {
  app.state().guarded([&] { cancel_press(); });
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
  return {static_cast<float>(state_->width_px) / state_->scale,
          static_cast<float>(state_->height_px) / state_->scale};
}

Size Window::measure(Size available) { return available; }

void Window::arrange(Rect bounds) {
  detail::Tree::set_bounds(*this, bounds);
  for (size_t i = 0; i < children().size(); ++i) {
    if (children()[i]->visible()) children()[i]->arrange(bounds);
  }
}

void Window::paint(Painter& p) { p.fill_rect(bounds(), theme().background); }

}  // namespace smalti
