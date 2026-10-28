// App and the app-wide state: backend, windows, fonts, theme, posted work, deferred removal.
#include "smalti/app.hpp"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>

#include "core/state.hpp"

namespace smalti {

namespace {
App* g_app = nullptr;
}  // namespace

namespace detail {

AppState& app_state() { return App::current().state(); }

AppState::AppState(App& app_ref, const AppOptions& options)
    : app(app_ref),
      backend(platform::create_backend(options.backend, options.presentation, *this)),
      system(backend->system_info()),
      theme(options.theme ? *options.theme : system.dark_mode ? theme_dark() : theme_light()),
      fonts(load_embedded_fonts()) {}

AppState::~AppState() = default;

void AppState::on_wake() {
  std::vector<Callback<void()>> work;
  {
    const std::lock_guard lock(posted_mutex);
    work.swap(posted);
  }
  for (auto& fn : work) guarded([&] { fn(); });
}

void AppState::schedule_removal(WidgetId id) { removals.push_back(id); }

void AppState::flush_removals() {
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
        const Rect b = w->bounds();
        const float o = w->overflow();
        ws->invalidate({b.x - o, b.y - o, b.width + 2 * o, b.height + 2 * o});
      }
      Tree::detach(*w);  // the returned owner goes out of scope here: the subtree is destroyed
    }
  }
}

void AppState::window_closed(Window* w) {
  const auto it = std::find_if(windows.begin(), windows.end(), [&](const auto& p) { return p.get() == w; });
  if (it == windows.end()) return;
  // Not destroyed here: the platform code that reported the close may still be on the stack.
  closing.push_back(std::move(*it));
  windows.erase(it);
  if (windows.empty() && running) backend->quit();
}

void AppState::reap_closed() { closing.clear(); }

void AppState::after_events() {
  flush_removals();  // removals requested outside any event (from setup code, say)
  reap_closed();
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
  auto window = std::make_unique<Window>(*this, options);
  const Ref<Window> ref = Ref<Window>::from_id(window->id());
  window->state().native->show();
  state_->windows.push_back(std::move(window));
  return ref;
}

int App::run() {
  detail::AppState& s = *state_;
  s.flush_removals();
  s.reap_closed();
  if (s.windows.empty()) return s.exit_code;
  s.running = true;
  s.backend->run();
  s.running = false;
  s.reap_closed();
  if (s.pending_exception) {
    std::exception_ptr e = s.pending_exception;
    s.pending_exception = nullptr;
    std::rethrow_exception(e);
  }
  return s.exit_code;
}

void App::quit(int exit_code) {
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
  state_->theme = theme;
  for (auto& w : state_->windows) {
    w->state().native->set_dark_frame(theme.dark);
    w->state().invalidate_layout();
  }
}

const Theme& App::theme() const { return state_->theme; }

MemoryStats App::memory_stats() const {
  MemoryStats m;
  m.widgets = detail::Registry::instance().live();
  m.glyph_cache_bytes = state_->glyphs.bytes();
  for (const auto& w : state_->windows) {
    const detail::WindowState& ws = w->state();
    m.frame_buffer_bytes += static_cast<size_t>(ws.width_px) * static_cast<size_t>(ws.height_px) * sizeof(uint32_t);
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
