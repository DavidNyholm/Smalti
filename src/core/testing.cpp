// The test driver: simulated input and a fake clock against the headless backend.
#include "smalti/testing.hpp"

#include <chrono>

#include "core/state.hpp"
#include "platform/headless.hpp"

namespace smalti::testing {

namespace {

platform::HeadlessWindow& native(Ref<Window> w) {
  auto* hw = dynamic_cast<platform::HeadlessWindow*>(w->state().native.get());
  if (!hw)
    detail::contract_violation("testing::Driver needs an App created with Backend::headless");
  return *hw;
}

platform::HeadlessBackend& backend() {
  auto* hb = dynamic_cast<platform::HeadlessBackend*>(detail::app_state().backend.get());
  if (!hb)
    detail::contract_violation("testing::Driver needs an App created with Backend::headless");
  return *hb;
}

Point device(Ref<Window> w, Point p) { return {p.x * w->scale(), p.y * w->scale()}; }

// A callback that threw during a driver call fails that call, as it would fail App::run(): a test
// must not pass while one failed.
void rethrow_failure() {
  detail::AppState& s = detail::app_state();
  if (!s.pending_exception) return;
  const std::exception_ptr e = s.pending_exception;
  s.pending_exception = nullptr;
  std::rethrow_exception(e);
}

}  // namespace

Driver::Driver(Ref<Window> window) : window_(window) {
  native(window_);  // checks the backend now rather than at the first event
  frame();
}

void Driver::move(Point p) {
  native(window_).events().on_mouse_move(device(window_, p), {});
  backend().host().after_events();
  rethrow_failure();
}

void Driver::press(Point p, MouseButton button) {
  native(window_).events().on_mouse_button(button, true, device(window_, p), {}, 1);
  backend().host().after_events();
  rethrow_failure();
}

void Driver::release(Point p, MouseButton button) {
  native(window_).events().on_mouse_button(button, false, device(window_, p), {}, 1);
  backend().host().after_events();
  rethrow_failure();
}

void Driver::click(Point p) {
  move(p);
  press(p);
  if (window_) release(p);  // the press may have closed the window
}

void Driver::click(Ref<Widget> widget) {
  frame();  // make sure the widget has been laid out
  click(widget->bounds().center());
}

void Driver::wheel(Point p, Point delta) {
  native(window_).events().on_mouse_wheel(device(window_, p), delta, {});
  backend().host().after_events();
  rethrow_failure();
}

void Driver::key(Key k, Modifiers modifiers) {
  native(window_).events().on_key({k, true, false, modifiers});
  if (window_) native(window_).events().on_key({k, false, false, modifiers});
  backend().host().after_events();
  rethrow_failure();
}

void Driver::resize(Size logical, float scale) {
  native(window_).resize(logical, scale);
  backend().host().after_events();
  rethrow_failure();
}

void Driver::advance(std::chrono::milliseconds time) {
  backend().advance(std::chrono::duration_cast<std::chrono::nanoseconds>(time).count());
  rethrow_failure();
}

void Driver::frame() {
  if (!window_) return;
  backend().poll_file_watch();
  backend().host().run_timers(backend().now_ns());
  native(window_).run_frame(backend().now_ns());
  backend().host().after_events();
  rethrow_failure();
}

void Driver::idle() {
  backend().poll_file_watch();
  backend().host().run_timers(backend().now_ns());
  backend().host().after_events();
  rethrow_failure();
}

int Driver::width() const { return native(window_).bitmap().width; }
int Driver::height() const { return native(window_).bitmap().height; }

uint32_t Driver::pixel(int x, int y) const {
  const auto& b = native(window_).bitmap();
  if (x < 0 || y < 0 || x >= b.width || y >= b.height) return 0;
  return b.pixels[static_cast<size_t>(y) * static_cast<size_t>(b.width) + static_cast<size_t>(x)];
}

bool Driver::frame_requested() const { return native(window_).frame_requested(); }

int Driver::frames_drawn() const { return window_->state().frames_drawn; }

Cursor Driver::cursor() const { return native(window_).cursor(); }

}  // namespace smalti::testing
