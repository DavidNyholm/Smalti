#include "platform/headless.hpp"

#include <algorithm>
#include <cmath>

namespace smalti::platform {

HeadlessWindow::HeadlessWindow(HeadlessBackend& backend, const WindowOptions& options,
                               WindowEvents& events)
    : backend_(backend), events_(events) {
  resize(options.size, 1.0f);
  backend_.windows_.push_back(this);  // last: if anything above throws, the backend never saw us
}

HeadlessWindow::~HeadlessWindow() { backend_.forget(this); }

void HeadlessWindow::present(std::span<const RectI>) { ++presents_; }

void HeadlessWindow::resize(Size logical, float scale) {
  scale_ = scale;
  const int w = std::max(1, static_cast<int>(std::lround(logical.width * scale)));
  const int h = std::max(1, static_cast<int>(std::lround(logical.height * scale)));
  bitmap_ = detail::Bitmap(w, h);
  events_.on_resize(w, h, scale);
}

bool HeadlessWindow::run_frame(int64_t now_ns) {
  if (!requested_) return false;
  requested_ = false;
  events_.on_frame(now_ns);
  return true;
}

std::unique_ptr<NativeWindow> HeadlessBackend::create_window(const WindowOptions& options,
                                                             WindowEvents& events) {
  return std::make_unique<HeadlessWindow>(*this, options, events);
}

void HeadlessBackend::forget(HeadlessWindow* w) { std::erase(windows_, w); }

void HeadlessBackend::advance(int64_t ns) {
  const int64_t end = now_ + ns;
  while (now_ < end) {
    now_ = std::min(end, now_ + kFrameNs);
    // By index: closed windows are destroyed only in after_events, so the list cannot shrink
    // during these frames, and windows opened by a frame simply wait for the next one.
    const size_t count = windows_.size();
    for (size_t i = 0; i < count && i < windows_.size(); ++i) windows_[i]->run_frame(now_);
    host_.after_events();
  }
}

void HeadlessBackend::run() {
  // With no screen and no input, run until quit() or until nothing is left to do: no frame
  // requested and no work posted. Each pass is one 60 Hz frame on the fake clock.
  quit_ = false;
  while (!quit_) {
    bool busy = false;
    if (woken_.exchange(false)) {
      host_.on_wake();
      busy = true;
    }
    for (HeadlessWindow* w : windows_) busy |= w->frame_requested();
    if (!busy) break;
    advance(kFrameNs);
  }
}

std::unique_ptr<PlatformBackend> create_headless_backend(BackendHost& host) {
  return std::make_unique<HeadlessBackend>(host);
}

}  // namespace smalti::platform
