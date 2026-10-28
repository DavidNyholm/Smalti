// App: owns the windows, the event loop, the fonts and the theme. One App per process.
#pragma once

#include <cstddef>
#include <memory>

#include "smalti/callback.hpp"
#include "smalti/ref.hpp"
#include "smalti/theme.hpp"
#include "smalti/window.hpp"

namespace smalti {

enum class Backend : uint8_t {
  automatic,  // Win32 on Windows; X11 on Linux when a display is available, else headless
  win32,
  x11,
  headless,   // no screen: windows draw into memory; for tests and image rendering
};

enum class Presentation : uint8_t {
  automatic,  // the CPU path for the first frame, then the GPU compositor when it is ready
  cpu,        // never create a GPU device: the smallest memory footprint
};

struct AppOptions {
  Backend backend = Backend::automatic;
  Presentation presentation = Presentation::automatic;
  const Theme* theme = nullptr;  // nullptr: Dark or Light, following the system setting
};

struct MemoryStats {
  size_t widgets = 0;
  size_t frame_buffer_bytes = 0;  // pixels the CPU draws into, all windows
  size_t glyph_cache_bytes = 0;
  size_t gpu_texture_bytes = 0;   // our own textures and swap chains, not the driver's overhead
};

namespace detail {
struct AppState;
}

class App {
 public:
  explicit App(AppOptions options = {});
  ~App();
  App(const App&) = delete;
  App& operator=(const App&) = delete;

  Ref<Window> open_window(const WindowOptions& options = {});

  // Runs until the last window closes or quit() is called; returns the exit code. An exception
  // thrown by a callback stops the loop and is rethrown from here.
  int run();
  void quit(int exit_code = 0);

  // The only thread-safe call: runs `fn` on the UI thread, soon.
  void post(Callback<void()> fn);

  void set_theme(const Theme& theme);
  const Theme& theme() const;

  MemoryStats memory_stats() const;

  static App& current();  // the App of this process; a contract violation if there is none
  detail::AppState& state() { return *state_; }

 private:
  std::unique_ptr<detail::AppState> state_;
};

}  // namespace smalti
