// The porting interface: what a platform backend implements (NativeWindow, Backend) and what it
// calls back into (WindowEvents, BackendHost). A new platform is a new implementation of these
// two classes; nothing above this layer knows which one is running.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "render/canvas.hpp"
#include "smalti/app.hpp"
#include "smalti/callback.hpp"
#include "smalti/input.hpp"
#include "smalti/window.hpp"

namespace smalti::platform {

using detail::PixelBuffer;
using detail::RectI;

// Implemented by the core. Positions are in device pixels; the core converts.
class WindowEvents {
 public:
  virtual void on_mouse_move(Point device, Modifiers mods) = 0;
  virtual void on_mouse_button(MouseButton button, bool down, Point device, Modifiers mods,
                               int clicks) = 0;
  virtual void on_mouse_wheel(Point device, Point delta_logical, Modifiers mods) = 0;
  virtual void on_mouse_leave() = 0;
  virtual void on_key(const KeyEvent& e) = 0;
  // Text the user typed, in UTF-8, after the key events that typed it. The core drops control
  // characters and invalid UTF-8, so a backend may pass on what the system gives it.
  virtual void on_text(std::string_view utf8) = 0;
  virtual void on_focus(bool gained) = 0;
  // The system took the mouse capture away (another window grabbed it): the press is over.
  virtual void on_capture_lost() = 0;
  // The client area changed size or scale, or the frame buffer was replaced: draw everything.
  virtual void on_resize(int width_px, int height_px, float scale) = 0;
  // Draw now. Called once per request_frame(), paced to the display.
  virtual void on_frame(int64_t frame_time_ns) = 0;
  virtual void on_close_request() = 0;

 protected:
  ~WindowEvents() = default;
};

class NativeWindow {
 public:
  virtual ~NativeWindow() = default;
  virtual void show() = 0;
  virtual void set_title(std::string_view utf8) = 0;
  // Where the core draws. Valid until the next on_resize.
  virtual PixelBuffer frame_buffer() = 0;
  // Puts the given rectangles of the frame buffer on screen.
  virtual void present(std::span<const RectI> dirty) = 0;
  // Asks for one on_frame call, at the next moment the display can take a frame.
  virtual void request_frame() = 0;
  virtual bool frame_requested() const = 0;
  virtual void set_cursor(Cursor cursor) = 0;
  virtual void capture_mouse(bool capture) = 0;
  virtual void set_dark_frame(bool dark) = 0;
  // Switches the input method (an IME) on while a widget that takes text has focus, and off
  // otherwise; while on, its windows go at `caret` (device pixels). Called again when the caret
  // moves. Switching off may discard text still being composed.
  virtual void set_text_input(bool active, RectI caret) = 0;
  virtual float scale() const = 0;
  virtual size_t gpu_bytes() const { return 0; }
};

struct SystemInfo {
  bool dark_mode = true;
  bool reduced_motion = false;
  // How long a text caret stays shown, and then hidden; 0 if it should not blink. 530 ms is
  // Windows' default, and where the system has no setting of its own.
  int caret_blink_ms = 530;
};

// A window's width or height in device pixels from a logical size: whole, at least 1 and at
// most kMaxWindowPixels, whatever the caller passed (NaN and infinity included), so it always
// converts to an integer safely.
inline constexpr int kMaxWindowPixels = 1 << 15;
int window_pixels(float logical, float scale);
// The display scale for a dots-per-inch setting written as text (X11's Xft.dpi): 1 unless it
// is a whole, finite number inside the range screens have.
float scale_from_dpi_text(const char* text);

// Implemented by the App: what the backend calls that is not about one window.
class BackendHost {
 public:
  virtual void on_wake() = 0;  // run work posted from other threads
  // Called by the event loop after each batch of OS events, outside any window procedure: the
  // safe point where closed windows are destroyed.
  virtual void after_events() = 0;
  // The earliest time, on now_ns()'s clock, that the host has timed work for (a widget's timed
  // redraw, the pause before a theme reloads); INT64_MAX when it has none. The loop's wait
  // ends by then, so timed work needs no polling.
  virtual int64_t next_deadline_ns() const = 0;
  // Runs the timed work due by `now_ns`. Called by the loop on every turn, before frames, so
  // a redraw it asks for is drawn in the same turn.
  virtual void run_timers(int64_t now_ns) = 0;

 protected:
  ~BackendHost() = default;
};

class PlatformBackend {
 public:
  virtual ~PlatformBackend() = default;
  virtual std::unique_ptr<NativeWindow> create_window(const WindowOptions& options,
                                                      WindowEvents& events) = 0;
  virtual void run() = 0;  // returns after quit()
  virtual void quit() = 0;
  virtual void wake() = 0;  // thread-safe: calls host.on_wake() on the UI thread soon
  virtual int64_t now_ns() const = 0;
  virtual SystemInfo system_info() const = 0;
  // Calls `changed` on the UI thread, from the loop, whenever `file` changes: the watch joins the
  // loop's wait (platform/file_watch.hpp), so it costs nothing until something changes. One file
  // at a time: a new call replaces the last. Throws std::runtime_error, with nothing watched, if
  // the file's folder cannot be watched.
  virtual void watch_file(const std::filesystem::path& file, Callback<void()> changed) = 0;
  virtual void stop_watching_file() = 0;
  // The system clipboard's text in UTF-8, as the system holds it (the core makes it plain:
  // detail::plain_text); nullopt if it holds no text, more than detail::kMaxClipboardBytes, or
  // cannot be read in time. Synchronous: a paste needs the text now.
  virtual std::optional<std::string> clipboard_text() = 0;
  // Replaces what the system clipboard holds with this text. The core passes plain text ("\n"
  // line endings); the backend writes what its system expects. False if the system refused.
  virtual bool set_clipboard_text(std::string_view plain) = 0;
};

std::unique_ptr<PlatformBackend> create_backend(Backend kind, Presentation presentation,
                                                BackendHost& host);
std::unique_ptr<PlatformBackend> create_headless_backend(BackendHost& host);
#if !defined(SMALTI_HEADLESS_ONLY)
#if defined(_WIN32)
std::unique_ptr<PlatformBackend> create_win32_backend(Presentation presentation, BackendHost& host);
#elif defined(__linux__)
std::unique_ptr<PlatformBackend> create_x11_backend(
    BackendHost& host);  // nullptr without a display
#endif
#endif

}  // namespace smalti::platform
