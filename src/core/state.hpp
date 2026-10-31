// The internal state behind App and Window: the widget registry, the per-window pipeline
// (input, layout, damage, paint, present) and the app-wide resources shared by all windows.
#pragma once

#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include "platform/platform.hpp"
#include "render/canvas.hpp"
#include "render/text.hpp"
#include "smalti/app.hpp"
#include "smalti/window.hpp"
#include "theme/widget_styles.hpp"

namespace smalti::detail {

// Maps WidgetIds to live widgets. Generations make an old id useless once its slot is reused.
class Registry {
 public:
  static Registry& instance();
  WidgetId allocate(Widget* widget);
  void release(WidgetId id);
  Widget* find(WidgetId id) const;  // nullptr if removed
  void check_thread() const;        // a contract violation off the UI thread
  void set_ui_thread(std::optional<std::thread::id> id) { ui_thread_ = id; }
  size_t live() const { return live_; }

 private:
  struct Slot {
    Widget* widget = nullptr;
    uint32_t generation = 0;
  };
  std::vector<Slot> slots_{Slot{}};  // slot 0 is never used, so a default WidgetId is never live
  std::vector<uint32_t> free_;
  std::optional<std::thread::id> ui_thread_;
  size_t live_ = 0;
};

// What a Painter draws with.
struct PaintState {
  Canvas* canvas = nullptr;
  GlyphCache* glyphs = nullptr;
  const FontSet* fonts = nullptr;
  const Theme* theme = nullptr;
  float scale = 1;
  std::vector<RectI> clips;
  std::vector<Point> scratch;  // reused for polylines
};

struct WindowState final : platform::WindowEvents {
  WindowState(App& app, Window& window, const WindowOptions& options);
  ~WindowState();

  // From widgets.
  void invalidate(const Rect& logical);
  void invalidate_all();
  void invalidate_layout();
  void animate_until(WidgetId id, int64_t end_ns);
  void redraw_at(WidgetId id, int64_t time_ns);
  int64_t next_redraw() const;    // INT64_MAX when no timed redraw is waiting
  void run_redraws(int64_t now);  // redraws the widgets whose time has come
  int64_t now() const;
  void focus(Widget* w);
  void request_frame();
  // Switches the input method on or off and places it, as the focused widget needs; tells the
  // platform only when that changes.
  void sync_text_input();
  // Ends the mouse press without a release: the pressed widget gets MouseEvent::Kind::cancel.
  void cancel_press();
  // Drops focus, the press and the hover from `root` and its descendants, as they are being
  // hidden, disabled or removed.
  void release_subtree(const Widget& root);

  // platform::WindowEvents
  void on_mouse_move(Point device, Modifiers mods) override;
  void on_mouse_button(MouseButton button, bool down, Point device, Modifiers mods,
                       int clicks) override;
  void on_mouse_wheel(Point device, Point delta, Modifiers mods) override;
  void on_mouse_leave() override;
  void on_key(const KeyEvent& e) override;
  void on_text(std::string_view utf8) override;
  void on_focus(bool gained) override;
  void on_capture_lost() override;
  void on_resize(int width_px, int height_px, float scale) override;
  void on_frame(int64_t frame_time_ns) override;
  void on_close_request() override;

  // The pipeline, also used by the test driver.
  void draw(int64_t frame_time_ns);
  void draw_frame(int64_t frame_time_ns);  // draw() without the guards around it
  Widget* hit_test(Point logical) const;
  void set_hovered(Widget* w);
  void move_focus(bool backwards);

  App& app;
  Window& window;
  std::unique_ptr<platform::NativeWindow> native;
  std::string title;
  Size min_size;
  float scale = 1;
  int width_px = 0, height_px = 0;
  PixelBuffer frame;
  Canvas canvas;
  PaintState paint;

  std::vector<RectI> dirty;
  std::vector<RectI> drawing;  // the damage of the frame being painted; `dirty` collects the next
  bool layout_dirty = true;
  int64_t frame_time = 0;
  int frames_drawn = 0;
  bool in_draw = false;  // laying out or painting: no frame inside a frame, no widgets added

  WidgetId hovered, pressed, focused;
  MouseButton pressed_button = MouseButton::none;  // the button that started the press
  uint64_t press_serial = 0;   // moves on whenever a press ends, so nested dispatch can be seen
  bool press_pending = false;  // a press whose focus hooks run before any widget has taken it
  bool keyboard_mode = false;  // focus rings show only while navigating by keyboard
  bool window_focused = true;
  Point last_mouse{-1, -1};
  std::vector<std::pair<WidgetId, int64_t>> animating;  // widget, until when
  std::vector<std::pair<WidgetId, int64_t>> timed;      // widget, when to redraw it
  bool text_input_active = false;                       // as last told to the platform
  RectI text_input_caret{};
};

struct AppState final : platform::BackendHost {
  explicit AppState(App& app, const AppOptions& options);
  ~AppState();

  void on_wake() override;
  void after_events() override;
  int64_t next_deadline_ns() const override;
  void run_timers(int64_t now_ns) override;
  // The watched theme file (App::watch_theme).
  void watch_theme(const std::filesystem::path& file);
  void stop_watching_theme();
  void reload_theme();  // applies the file if it is valid, reports its errors if not
  void report_theme_errors(std::span<const ThemeError> errors);
  // Runs fn, catching anything it throws: the first exception stops the loop and is rethrown
  // from run(). Pending removals are processed once the outermost guarded call returns: a
  // callback that opens a nested event loop (a native message box) must not find its own
  // widget destroyed by an event dispatched inside that loop.
  template <class F>
  void guarded(F&& fn) {
    ++dispatch_depth;
    try {
      fn();
    } catch (...) {
      stop_with(std::current_exception());
    }
    --dispatch_depth;
    flush_removals();
  }
  void stop_with(std::exception_ptr e);  // keeps the first exception and ends the loop
  void schedule_removal(WidgetId id);
  void flush_removals();  // does nothing while an event is being dispatched
  void window_closed(Window* w);
  void reap_closed();  // destroys windows closed since the last call; not during dispatch

  App& app;
  std::unique_ptr<platform::PlatformBackend> backend;
  platform::SystemInfo system;
  Theme theme;
  WidgetStyles styles;  // the theme's widget styles, computed whenever the theme is set
  std::optional<std::filesystem::path> theme_file;  // watched, when set
  int64_t theme_reload_at = INT64_MAX;              // a change was reported: reload then
  Callback<void(std::span<const ThemeError>)> theme_error_handler;
  FontSet fonts;
  GlyphCache glyphs;
  std::vector<std::unique_ptr<Window>> windows;
  std::vector<std::unique_ptr<Window>> closing;  // closed, destroyed at the next safe point
  std::vector<WidgetId> removals;
  std::exception_ptr pending_exception;
  int exit_code = 0;
  int dispatch_depth = 0;  // guarded() calls on the stack
  bool running = false;
  bool draining_posted = false;

  std::mutex posted_mutex;
  std::vector<Callback<void()>> posted;
};

// Counts as an event being dispatched for as long as it lives, so removals requested meanwhile
// wait even if a hook it calls runs a nested event loop. For public setters that call into
// widgets (a cancelled press, a lost focus) and may be called outside any event.
class DispatchScope {
 public:
  explicit DispatchScope(AppState& state) : state_(state) { ++state_.dispatch_depth; }
  ~DispatchScope() { --state_.dispatch_depth; }
  DispatchScope(const DispatchScope&) = delete;
  DispatchScope& operator=(const DispatchScope&) = delete;

 private:
  AppState& state_;
};

// Visible all the way up: hiding a container hides what is in it.
bool shown(const Widget& w);
// Whether input may reach w: shown, and enabled all the way up.
bool eligible(const Widget& w);

// The core's access to the parts of Widget and Window that are not public API.
class Tree {
 public:
  static Window* window_of(const Widget& w) { return w.window_; }
  static Widget* parent_of(const Widget& w) { return w.parent_; }
  static WindowState* state_of(const Window& w) { return w.state_.get(); }
  static void set_window(Widget& w, Window* window) { w.window_ = window; }
  static void set_bounds(Widget& w, Rect r) { w.bounds_ = r; }
  // Takes `w` out of its parent and hands over ownership; nullptr for a window.
  static std::unique_ptr<Widget> detach(Widget& w);
};

WindowState* window_state_of(const Widget& w);
AppState& app_state();

}  // namespace smalti::detail
