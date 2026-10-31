// The headless backend: windows draw into memory and a fake clock drives frames. Used by the
// tests through smalti::testing::Driver, and by anything that renders without a screen.
#pragma once

#include <atomic>
#include <vector>

#include "platform/file_watch.hpp"
#include "platform/platform.hpp"

namespace smalti::platform {

class HeadlessBackend;

class HeadlessWindow final : public NativeWindow {
 public:
  HeadlessWindow(HeadlessBackend& backend, const WindowOptions& options, WindowEvents& events);
  ~HeadlessWindow() override;

  void show() override {}
  void set_title(std::string_view) override {}
  PixelBuffer frame_buffer() override { return bitmap_.view(); }
  void present(std::span<const RectI> dirty) override;
  void request_frame() override { requested_ = true; }
  bool frame_requested() const override { return requested_; }
  void set_cursor(Cursor c) override { cursor_ = c; }
  void capture_mouse(bool) override {}
  void set_dark_frame(bool) override {}
  void set_text_input(bool active, RectI caret) override {
    text_input_ = active;
    caret_ = caret;
  }
  float scale() const override { return scale_; }

  void resize(Size logical, float scale);
  // Runs the frame the core asked for, if any. Returns whether one ran.
  bool run_frame(int64_t now_ns);

  WindowEvents& events() { return events_; }
  const detail::Bitmap& bitmap() const { return bitmap_; }
  int presents() const { return presents_; }
  Cursor cursor() const { return cursor_; }
  bool text_input() const { return text_input_; }
  RectI text_caret() const { return caret_; }

 private:
  HeadlessBackend& backend_;
  WindowEvents& events_;
  detail::Bitmap bitmap_;
  float scale_ = 1;
  bool requested_ = false;
  int presents_ = 0;
  Cursor cursor_ = Cursor::arrow;
  bool text_input_ = false;
  RectI caret_{};
};

class HeadlessBackend final : public PlatformBackend {
 public:
  // A 60 Hz display: the step the clock takes per frame when run() or a test advances it.
  static constexpr int64_t kFrameNs = 16'666'667;

  explicit HeadlessBackend(BackendHost& host) : host_(host) {}

  std::unique_ptr<NativeWindow> create_window(const WindowOptions& options,
                                              WindowEvents& events) override;
  void run() override;
  void quit() override { quit_ = true; }
  void wake() override { woken_ = true; }
  int64_t now_ns() const override { return now_; }
  SystemInfo system_info() const override { return {}; }  // dark, motion on, caret blinks
  // A real watch on a real file, read without blocking whenever the fake clock moves or a
  // test calls poll_file_watch(): tests can save a file and see it reload.
  void watch_file(const std::filesystem::path& file, Callback<void()> changed) override {
    watched_.start(file, std::move(changed));
  }
  void stop_watching_file() override { watched_.stop(); }
  void poll_file_watch() { watched_.poll(); }
  // A clipboard of its own, in memory. Tests put text on it as another program would.
  std::optional<std::string> clipboard_text() override { return clipboard_; }
  bool set_clipboard_text(std::string_view plain) override {
    clipboard_ = std::string(plain);
    return true;
  }

  // Moves the clock forward by `ns`, running every requested frame on the way.
  void advance(int64_t ns);
  void forget(HeadlessWindow* w);
  BackendHost& host() { return host_; }

 private:
  friend class HeadlessWindow;
  BackendHost& host_;
  std::vector<HeadlessWindow*> windows_;
  WatchedFile watched_;
  std::optional<std::string> clipboard_;
  int64_t now_ = 0;
  bool quit_ = false;
  std::atomic<bool> woken_{false};
};

}  // namespace smalti::platform
