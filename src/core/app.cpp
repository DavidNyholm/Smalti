// App and the app-wide state: backend, windows, fonts, theme, posted work, deferred removal.
#include "smalti/app.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

#include "core/state.hpp"

namespace smalti {

namespace {
App* g_app = nullptr;
// A watched theme reloads this long after the last change the system reported: one save
// arrives as several reports (write, rename, attributes), and should reload once.
constexpr int64_t kThemeReloadDelayNs = 50'000'000;
}  // namespace

namespace detail {

AppState& app_state() { return App::current().state(); }

AppState::AppState(App& app_ref, const AppOptions& options)
    : app(app_ref),
      backend(platform::create_backend(options.backend, options.presentation, *this)),
      system(backend->system_info()),
      theme(options.theme      ? *options.theme
            : system.dark_mode ? theme_dark()
                               : theme_light()),
      styles(compute_widget_styles(theme)),
      fonts(load_embedded_fonts()) {}

AppState::~AppState() = default;

void AppState::on_wake() {
  // A posted function that runs a nested event loop (a native message box) can be woken into
  // again; the work waits for the drain further up the stack, so it always runs in the order it
  // was posted.
  if (draining_posted) return;
  std::vector<Callback<void()>> work;
  {
    const std::lock_guard lock(posted_mutex);
    work.swap(posted);
  }
  {
    draining_posted = true;
    struct Reset {
      bool& flag;
      ~Reset() { flag = false; }
    } reset{draining_posted};
    for (auto& fn : work) guarded([&] { fn(); });
  }
  // Only what was posted before this wake runs now. What came in meanwhile (a long job posting
  // its next piece) runs on the next wake, after input and frames have had their turn.
  bool more = false;
  {
    const std::lock_guard lock(posted_mutex);
    more = !posted.empty();
  }
  if (more && backend) backend->wake();
}

void AppState::stop_with(std::exception_ptr e) {
  if (!pending_exception) pending_exception = e;
  if (backend) backend->quit();
}

void AppState::schedule_removal(WidgetId id) { removals.push_back(id); }

void AppState::flush_removals() {
  if (dispatch_depth > 0) return;  // an outer event is still running; it flushes when it returns
  while (!removals.empty()) {
    std::vector<WidgetId> batch;
    batch.swap(removals);
    for (WidgetId id : batch) {
      Widget* w = Registry::instance().find(id);
      if (!w) continue;
      if (auto* win = dynamic_cast<Window*>(w)) {
        window_closed(win);
        continue;
      }
      if (auto* ws = window_state_of(*w)) {
        // This calls into widgets (a cancelled press, a lost focus). Counted as a dispatch, so a
        // removal they request joins this loop instead of starting a nested one.
        ++dispatch_depth;
        try {
          ws->release_subtree(*w);
        } catch (...) {
          stop_with(std::current_exception());
        }
        --dispatch_depth;
        const Rect b = w->bounds();
        const float o = w->overflow();
        ws->invalidate({b.x - o, b.y - o, b.width + 2 * o, b.height + 2 * o});
      }
      Tree::detach(*w);  // the returned owner goes out of scope here: the subtree is destroyed
    }
  }
}

void AppState::window_closed(Window* w) {
  const auto it =
      std::find_if(windows.begin(), windows.end(), [&](const auto& p) { return p.get() == w; });
  if (it == windows.end()) return;
  // Not destroyed here: the platform code that reported the close may still be on the stack.
  closing.push_back(std::move(*it));
  windows.erase(it);
  if (windows.empty() && running) backend->quit();
}

void AppState::reap_closed() {
  if (dispatch_depth == 0) closing.clear();
}

void AppState::after_events() {
  flush_removals();  // removals requested outside any event (from setup code, say)
  reap_closed();
}

int64_t AppState::next_deadline_ns() const {
  int64_t next = theme_reload_at;
  for (const auto& w : windows) next = std::min(next, w->state().next_redraw());
  return next;
}

void AppState::run_timers(int64_t now_ns) {
  if (theme_reload_at <= now_ns) {
    theme_reload_at = INT64_MAX;
    guarded([&] { reload_theme(); });  // the error handler is the app's code
  }
  // By index: a redraw cannot open or close windows, but stays safe if one ever does.
  for (size_t i = 0; i < windows.size(); ++i) windows[i]->state().run_redraws(now_ns);
}

void AppState::watch_theme(const std::filesystem::path& file) {
  stop_watching_theme();
  theme_file = file;
  reload_theme();
  try {
    backend->watch_file(file, [this] { theme_reload_at = backend->now_ns() + kThemeReloadDelayNs; });
  } catch (const std::runtime_error& e) {
    const ThemeError error{0, 0, e.what()};
    report_theme_errors({&error, 1});
  }
}

void AppState::stop_watching_theme() {
  backend->stop_watching_file();
  theme_file.reset();
  theme_reload_at = INT64_MAX;
}

void AppState::reload_theme() {
  if (!theme_file) return;
  const ThemeLoad r = load_theme(*theme_file);
  if (r.ok()) {
    app.set_theme(r.theme);
  } else {
    report_theme_errors(r.errors);
  }
}

void AppState::report_theme_errors(std::span<const ThemeError> errors) {
  if (theme_error_handler) {
    theme_error_handler(errors);
    return;
  }
  const std::u8string file8 = theme_file ? theme_file->u8string() : std::u8string();
  const std::string file(file8.begin(), file8.end());
  for (const ThemeError& e : errors) {
    std::fprintf(stderr, "smalti: theme %s:%d:%d: %s\n", file.c_str(), e.line, e.column,
                 e.message.c_str());
  }
}

}  // namespace detail

App::App(AppOptions options) {
  if (g_app) detail::contract_violation("only one App may exist at a time");
  g_app = this;
  detail::Registry::instance().set_ui_thread(std::this_thread::get_id());
  try {
    state_ = std::make_unique<detail::AppState>(*this, options);
  } catch (...) {
    g_app = nullptr;
    detail::Registry::instance().set_ui_thread(std::nullopt);
    throw;
  }
}

App::~App() {
  // Windows (and their widgets) go before the fonts and the backend they use.
  state_->windows.clear();
  state_->closing.clear();
  state_.reset();
  detail::Registry::instance().set_ui_thread(std::nullopt);
  g_app = nullptr;
}

Ref<Window> App::open_window(const WindowOptions& options) {
  detail::Registry::instance().check_thread();
  auto window = std::make_unique<Window>(*this, options);
  const Ref<Window> ref = Ref<Window>::from_id(window->id());
  window->state().native->show();
  state_->windows.push_back(std::move(window));
  return ref;
}

int App::run() {
  detail::Registry::instance().check_thread();
  detail::AppState& s = *state_;
  if (s.running) detail::contract_violation("App::run() was called while it is already running");
  // A callback that fails while this cleanup runs it is reported like one that fails later.
  const auto rethrow_pending = [&s] {
    if (!s.pending_exception) return;
    std::exception_ptr e = s.pending_exception;
    s.pending_exception = nullptr;
    std::rethrow_exception(e);
  };
  s.flush_removals();
  s.reap_closed();
  rethrow_pending();
  if (s.windows.empty()) return s.exit_code;
  s.running = true;
  try {
    s.backend->run();
  } catch (...) {
    s.running = false;  // the backend's own failure; the App can still be run again
    throw;
  }
  s.running = false;
  s.reap_closed();
  rethrow_pending();
  return s.exit_code;
}

void App::quit(int exit_code) {
  detail::Registry::instance().check_thread();
  state_->exit_code = exit_code;
  state_->backend->quit();
}

void App::post(Callback<void()> fn) {
  {
    const std::lock_guard lock(state_->posted_mutex);
    state_->posted.push_back(std::move(fn));
  }
  state_->backend->wake();
}

void App::set_theme(const Theme& theme) {
  detail::Registry::instance().check_thread();
  detail::WidgetStyles styles = detail::compute_widget_styles(theme);  // checks its rules first
  state_->theme = theme;
  state_->styles = std::move(styles);
  for (auto& w : state_->windows) {
    w->state().native->set_dark_frame(theme.dark);
    w->state().invalidate_layout();
  }
}

void App::watch_theme(const std::filesystem::path& file) {
  detail::Registry::instance().check_thread();
  state_->watch_theme(file);
}

void App::stop_watching_theme() {
  detail::Registry::instance().check_thread();
  state_->stop_watching_theme();
}

void App::on_theme_error(Callback<void(std::span<const ThemeError>)> handler) {
  detail::Registry::instance().check_thread();
  state_->theme_error_handler = std::move(handler);
}

const Theme& App::theme() const {
  detail::Registry::instance().check_thread();
  return state_->theme;
}

MemoryStats App::memory_stats() const {
  detail::Registry::instance().check_thread();
  MemoryStats m;
  m.widgets = detail::Registry::instance().live();
  m.glyph_cache_bytes = state_->glyphs.bytes();
  for (const auto& w : state_->windows) {
    const detail::WindowState& ws = w->state();
    m.frame_buffer_bytes +=
        static_cast<size_t>(ws.width_px) * static_cast<size_t>(ws.height_px) * sizeof(uint32_t);
    m.glyph_cache_bytes += ws.canvas.cache_bytes();
    m.gpu_texture_bytes += ws.native->gpu_bytes();
  }
  return m;
}

App& App::current() {
  if (!g_app) detail::contract_violation("there is no App: create one before using widgets");
  return *g_app;
}

}  // namespace smalti
