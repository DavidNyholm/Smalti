// Regressions for defects found in review round 3. Some only fail loudly under the sanitizers
// (the linux-asan preset).
#include <stdexcept>
#include <vector>

#include "check.hpp"
#include "font_builder.hpp"
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

// Focusable, and records every focus change it is told about. When it loses focus, it hides
// `hide_on_blur` if set.
class FocusProbe : public Box {
 public:
  FocusProbe() : Box({40, 20}) {}
  bool focusable() const override { return true; }
  void on_focus_changed(bool gained) override {
    told.push_back(gained);
    if (!gained && hide_on_blur) hide_on_blur->set_visible(false);
  }
  std::vector<bool> told;
  Ref<Widget> hide_on_blur;
};

// Takes presses; when its press is cancelled it closes its window and throws.
class CancelCloses : public Box {
 public:
  CancelCloses() : Box({60, 30}) {}
  bool on_mouse(const MouseEvent& e) override {
    if (e.kind == MouseEvent::Kind::cancel) {
      window()->close();
      throw std::runtime_error("cancel failed");
    }
    return e.kind != MouseEvent::Kind::move;
  }
};

}  // namespace

TEST(review3_select_outside_an_event_keeps_the_radio_alive_in_its_callback) {
  App app(headless());
  auto win = app.open_window({"t", {200, 100}});
  auto group = win->add<Column>();
  auto radio = group->add<RadioButton>("going");
  group->add<RadioButton>("staying");
  testing::Driver d(win);
  bool alive_after_nested_frame = false;
  radio->on_select([&] {
    radio->remove();
    d.frame();  // a nested loop, as a message box would run
    alive_after_nested_frame = static_cast<bool>(radio);
  });
  radio->select();  // from code, not from an event
  CHECK(alive_after_nested_frame);
  d.idle();
  CHECK(!radio);
}

TEST(review3_a_press_its_own_handler_cancelled_stays_cancelled) {
  App app(headless());
  auto win = app.open_window({"t", {300, 100}});
  auto slider = win->add<Column>(StackOptions{.padding = Insets::all(20)})
                    ->add<Slider>(SliderOptions{.min = 0, .max = 100, .value = 50});
  slider->on_change([slider](float) {
    slider->set_enabled(false);  // cancels the press...
    slider->set_enabled(true);   // ...even though it is enabled again straight away
  });
  testing::Driver d(win);
  const Rect b = slider->bounds();
  d.press({b.x + 8, b.center().y});
  const float after_press = slider->value();
  d.move({b.right() - 8, b.center().y});
  CHECK_EQ(slider->value(), after_press);
  d.release({b.right() - 8, b.center().y});
}

TEST(review3_a_span_over_a_zero_weight_stretch_column_keeps_its_width) {
  App app(headless());
  auto win = app.open_window({"t", {200, 100}});
  auto grid = win->add<Grid>(
      GridOptions{.columns = {GridTrack::fit(), GridTrack::stretch(0), GridTrack::stretch(1)},
                  .column_spacing = 0});
  auto span = grid->add<Box>(Size{100, 10});
  span->set_column_span(2);
  testing::Driver(win).frame();
  CHECK_NEAR(span->bounds().width, 100.0f, 0.5f);
}

TEST(review3_a_span_over_stretch_columns_gets_its_width) {
  App app(headless());
  auto win = app.open_window({"t", {200, 100}});
  auto grid = win->add<Grid>(
      GridOptions{.columns = {GridTrack::fit(), GridTrack::stretch(), GridTrack::stretch()},
                  .column_spacing = 0});
  grid->add<Box>(Size{20, 10});
  grid->add<Box>(Size{10, 10});
  grid->add<Box>(Size{10, 10});
  auto span = grid->add<Box>(Size{150, 10});
  span->set_column_span(2);
  testing::Driver(win).frame();
  CHECK(span->bounds().width >=
        149.5f);  // 100 + 50: the fit column grows, the stretch columns share the rest
}

TEST(review3_focus_taken_back_by_the_old_widget_sends_no_stale_gain) {
  App app(headless());
  auto win = app.open_window({"t", {200, 100}});
  auto col = win->add<Column>();
  auto a = col->add<FocusProbe>();
  auto b = col->add<FocusProbe>();
  a->hide_on_blur = b;
  testing::Driver d(win);
  a->focus();
  b->focus();  // a's blur hides b on the way
  CHECK(!b->has_focus());
  for (bool gained : b->told) CHECK(!gained);  // b never heard that it gained focus
}

TEST(review3_a_far_away_glyph_is_not_rasterized) {
  // Three points on top of each other, far out: no area to speak of, but at this size the
  // coordinates are beyond what an int holds.
  using namespace font_builder;
  const Bytes far = simple_glyph({{32767, 32767}, {32767, 32767}, {32767, 32767}});
  const detail::Font f(font({{}, far}, {{"cmap", cmap12({{65, 65, 1}})}}, 16));
  detail::GlyphCache cache;
  CHECK(cache.get(f, 1, 2.0e6f, 0, 1.0f).empty());
}

TEST(review3_an_exception_during_startup_cleanup_comes_out_of_run) {
  App app(headless());
  auto win = app.open_window({"t", {200, 100}});
  auto w = win->add<Column>(StackOptions{.padding = Insets::all(10)})->add<CancelCloses>();
  testing::Driver d(win);
  d.press(w->bounds().center());
  w->remove();  // outside any event: the removal waits for run(), which cancels the press
  bool threw = false;
  try {
    app.run();
  } catch (const std::runtime_error&) {
    threw = true;
  }
  CHECK(threw);
}
