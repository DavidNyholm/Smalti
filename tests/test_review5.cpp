// Regressions for defects found in review round 5. Some only fail loudly under the sanitizers
// (the linux-asan preset).
#include <stdexcept>

#include "check.hpp"
#include "core/state.hpp"
#include "render/canvas.hpp"
#include "render/text.hpp"
#include "support.hpp"

using namespace smalti;

namespace {

class Box : public Widget {
 public:
  explicit Box(Size size) : size_(size) {}
  Size measure(Size) override { return size_; }

 private:
  Size size_;
};

// Focusable and takes presses; the first time it gains focus it pumps the release of the press
// that focused it, as a focus hook that runs a nested loop might.
class ReleasesOnFocus : public Box {
 public:
  explicit ReleasesOnFocus(testing::Driver** driver) : Box({60, 30}), driver_(driver) {}
  bool focusable() const override { return true; }
  void on_focus_changed(bool gained) override {
    if (gained && !done_) {
      done_ = true;
      (*driver_)->release(bounds().center());
    }
  }
  bool on_mouse(const MouseEvent& e) override {
    if (e.kind == MouseEvent::Kind::press) ++presses;
    return e.kind != MouseEvent::Kind::move;
  }
  int presses = 0;

 private:
  testing::Driver** driver_;
  bool done_ = false;
};

// Takes presses; its cancel handler records that it ran, then throws.
class CancelThrows : public Box {
 public:
  CancelThrows() : Box({60, 30}) {}
  bool on_mouse(const MouseEvent& e) override {
    if (e.kind == MouseEvent::Kind::cancel) {
      entered = true;
      throw std::runtime_error("cancel failed");
    }
    return e.kind != MouseEvent::Kind::move;
  }
  bool entered = false;
};

// Asks for the hand cursor, and hides itself the moment it gains the hover.
class HidesOnHover : public Box {
 public:
  HidesOnHover() : Box({60, 30}) {}
  Cursor cursor() const override { return Cursor::hand; }
  void on_hover_changed(bool hovered) override {
    if (hovered) set_visible(false);
  }
};

class Painting : public Box {
 public:
  explicit Painting(Callback<void(Painter&, Rect)> fn) : Box({100, 40}), fn_(std::move(fn)) {}
  void paint(Painter& p) override { fn_(p, bounds()); }

 private:
  Callback<void(Painter&, Rect)> fn_;
};

}  // namespace

TEST(review5_a_circle_far_bigger_than_the_window_draws_safely) {
  App app(headless());
  auto win = app.open_window({"t", {200, 100}});
  win->add<Column>()->add<Painting>(
      [](Painter& p, Rect) { p.fill_circle({0, 0}, 1e20f, Color::hex(0x00FF00)); });
  testing::Driver d(win);
  CHECK(d.frames_drawn() >= 1);
}

TEST(review5_a_release_pumped_by_a_focus_hook_ends_the_press) {
  App app(headless());
  auto win = app.open_window({"t", {200, 100}});
  testing::Driver d(win);
  testing::Driver* driver = &d;
  auto w =
      win->add<Column>(StackOptions{.padding = Insets::all(10)})->add<ReleasesOnFocus>(&driver);
  d.frame();
  d.press(w->bounds().center());
  CHECK_EQ(w->presses, 0);  // the button was up before any widget could take the press
}

TEST(review5_idle_hands_back_an_exception_from_cleanup) {
  App app(headless());
  auto win = app.open_window({"t", {200, 100}});
  auto w = win->add<Column>(StackOptions{.padding = Insets::all(10)})->add<CancelThrows>();
  testing::Driver d(win);
  d.press(w->bounds().center());
  w->remove();  // outside any event: the removal, and its cancel, run at the next safe point
  bool threw = false;
  try {
    d.idle();
  } catch (const std::runtime_error&) {
    threw = true;
  }
  CHECK(threw);
}

TEST(review5_text_far_off_the_canvas_still_reports_where_it_ends) {
  const detail::FontSet fonts = detail::load_embedded_fonts();
  detail::Bitmap target(100, 100);
  detail::Canvas canvas;
  canvas.set_target(target.view());
  detail::GlyphCache cache;
  const float size = 14;
  const float width = detail::text_width(*fonts.regular, size, "abc");
  const float end = detail::draw_text(canvas, cache, *fonts.regular, size, {10, 16777216.0f}, "abc",
                                      Color::hex(0xFFFFFF), TextTuning{});
  CHECK_NEAR(end, 10 + width, 0.5f);
}

TEST(review5_a_widget_that_hides_on_hover_leaves_the_arrow) {
  App app(headless());
  auto win = app.open_window({"t", {200, 100}});
  auto w = win->add<Column>(StackOptions{.padding = Insets::all(10)})->add<HidesOnHover>();
  testing::Driver d(win);
  d.move(w->bounds().center());
  CHECK(d.cursor() == Cursor::arrow);  // the hand belonged to a widget that is gone
}

// Findings 6 and 7, fixed in 0.2 by solving the spans together.
TEST(review5_overlapping_spans_over_fit_columns_fit_within_the_grid) {
  // Grown one span at a time, the fit columns took 50, 75 and 25 and pushed the field out.
  App app(headless());
  auto win = app.open_window({"t", {120, 100}});
  auto grid = win->add<Grid>(GridOptions{
      .columns = {GridTrack::fit(), GridTrack::fit(), GridTrack::fit(), GridTrack::stretch()},
      .column_spacing = 0});
  auto first = grid->add<Box>(Size{100, 10});
  first->set_column_span(2);
  grid->add<Box>(Size{0, 10});
  grid->add<Box>(Size{0, 10});
  grid->add<Box>(Size{0, 10});
  auto second = grid->add<Box>(Size{100, 10});
  second->set_column_span(2);
  auto field = grid->add<Box>(Size{0, 10});
  testing::Driver(win).frame();
  CHECK(first->bounds().width >= 99.5f);
  CHECK(second->bounds().width >= 99.5f);
  CHECK(field->bounds().right() <= 120.5f);  // 0, 100 and 0 leave the field 20
}

TEST(review5_a_span_over_the_stretch_column_is_not_left_short) {
  // Grown evenly, the first span's columns took 25 each, and the second span, over the stretch
  // column and the second of them, got 25 + 150 of its 180.
  App app(headless());
  auto win = app.open_window({"t", {200, 100}});
  auto grid = win->add<Grid>(GridOptions{
      .columns = {GridTrack::fit(), GridTrack::fit(), GridTrack::stretch()}, .column_spacing = 0});
  auto first = grid->add<Box>(Size{50, 10});
  first->set_column_span(2);
  grid->add<Box>(Size{0, 10});
  grid->add<Box>(Size{0, 10});
  auto second = grid->add<Box>(Size{180, 10});
  second->set_column_span(2);
  testing::Driver(win).frame();
  CHECK(first->bounds().width >= 49.5f);
  CHECK(second->bounds().width >= 179.5f);
  CHECK(second->bounds().right() <= 200.5f);
}
