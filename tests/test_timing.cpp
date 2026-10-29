// Timed redraws: a widget asks for one frame at a time instead of a frame every refresh, so a
// blinking caret costs two frames a second and an idle window nothing.
#include "check.hpp"
#include "support.hpp"

using namespace smalti;
using namespace std::chrono_literals;

namespace {

constexpr auto kBlink = 500ms;

// Draws itself, and while blinking asks to be drawn again after kBlink, as a caret will.
class Blinker : public Widget {
 public:
  int paints = 0;
  bool blinking = true;
  Size measure(Size) override { return {10, 10}; }
  void paint(Painter&) override {
    ++paints;
    if (blinking) redraw_after(kBlink);
  }
  void ask_at(int64_t time_ns) { redraw_at(time_ns); }
  void ask_after(std::chrono::nanoseconds delay) { redraw_after(delay); }
};

}  // namespace

TEST(timing_a_timed_redraw_costs_one_frame_each_time) {
  App app(headless());
  auto win = app.open_window({"t", {100, 100}});
  auto blinker = win->add<Blinker>();
  testing::Driver d(win);
  d.frame();  // the first layout asks for one more frame
  const int paints = blinker->paints, frames = d.frames_drawn();
  CHECK(paints > 0);
  CHECK(!d.frame_requested());  // nothing to draw until the blink is due
  // Due at 0.5, 1.0, 1.5 and 2.0 s: four frames, not one every refresh.
  d.advance(2100ms);
  CHECK_EQ(blinker->paints - paints, 4);
  CHECK_EQ(d.frames_drawn() - frames, 4);
  // Once the widget stops asking, it costs nothing.
  blinker->blinking = false;
  d.advance(1000ms);
  CHECK_EQ(blinker->paints - paints, 5);  // the redraw already asked for, then none
  d.advance(5000ms);
  CHECK_EQ(blinker->paints - paints, 5);
}

TEST(timing_the_earlier_redraw_stays) {
  App app(headless());
  auto win = app.open_window({"t", {100, 100}});
  auto blinker = win->add<Blinker>();
  blinker->blinking = false;
  testing::Driver d(win);
  const int paints = blinker->paints;
  blinker->ask_after(300ms);
  blinker->ask_after(900ms);  // later than the waiting one: it stays at 300 ms
  d.advance(400ms);
  CHECK_EQ(blinker->paints - paints, 1);
  d.advance(1000ms);
  CHECK_EQ(blinker->paints - paints, 1);
}

TEST(timing_a_removed_or_hidden_widget_is_not_redrawn) {
  App app(headless());
  auto win = app.open_window({"t", {100, 100}});
  auto gone = win->add<Blinker>();
  auto hidden = win->add<Blinker>();
  gone->blinking = hidden->blinking = false;
  testing::Driver d(win);
  gone->ask_after(100ms);
  hidden->ask_after(100ms);
  const int hidden_paints = hidden->paints;
  gone->remove();
  hidden->set_visible(false);
  d.idle();
  const int frames = d.frames_drawn();
  d.advance(500ms);
  CHECK_EQ(hidden->paints, hidden_paints);
  // The hide drew one frame; the due redraws drew nothing more.
  CHECK(d.frames_drawn() - frames <= 1);
  CHECK(!d.frame_requested());
}

TEST(timing_a_delay_too_long_for_the_clock_never_comes) {
  App app(headless());
  auto win = app.open_window({"t", {100, 100}});
  auto blinker = win->add<Blinker>();
  blinker->blinking = false;
  testing::Driver d(win);
  const int paints = blinker->paints;
  blinker->ask_after(std::chrono::nanoseconds::max());  // saturates instead of overflowing
  blinker->ask_at(INT64_MAX);
  d.advance(1000ms);
  CHECK_EQ(blinker->paints, paints);
  // A time in the past is due at once.
  blinker->ask_at(-1);
  d.frame();
  CHECK_EQ(blinker->paints - paints, 1);
}
