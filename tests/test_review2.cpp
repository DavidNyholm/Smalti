// Regressions for defects found in review round 2, several of them gaps in the round-1 fixes.
// Some only fail loudly under AddressSanitizer (the linux-asan preset).
#include <stdexcept>
#include <thread>

#include "check.hpp"
#include "core/state.hpp"
#include "font_builder.hpp"
#include "render/text.hpp"
#include "support.hpp"

using namespace smalti;
using namespace std::chrono_literals;

namespace {

// A widget of a fixed size that paints nothing unless told to.
class Box : public Widget {
 public:
  explicit Box(Size size) : size_(size) {}
  Size measure(Size) override { return size_; }

 private:
  Size size_;
};

// Takes presses; when its press is cancelled, it removes itself and pumps a nested frame, as a
// handler that opens a native message box would.
class CancelThenNest : public Box {
 public:
  explicit CancelThenNest(testing::Driver** driver) : Box({60, 30}), driver_(driver) {}
  bool on_mouse(const MouseEvent& e) override {
    if (e.kind == MouseEvent::Kind::cancel) {
      remove();
      (*driver_)->frame();
    }
    return e.kind != MouseEvent::Kind::move;
  }

 private:
  testing::Driver** driver_;
};

// The first time it paints, it requests another region and pumps a nested frame.
class PaintThenNest : public Box {
 public:
  PaintThenNest(testing::Driver** driver, Ref<Widget> other)
      : Box({60, 30}), driver_(driver), other_(other) {}
  void paint(Painter&) override {
    if (done_) return;
    done_ = true;
    other_->invalidate();
    (*driver_)->frame();
  }

 private:
  testing::Driver** driver_;
  Ref<Widget> other_;
  bool done_ = false;
};

// Adds a sibling the first time it paints.
class PaintAdds : public Box {
 public:
  PaintAdds() : Box({60, 30}) {}
  void paint(Painter&) override {
    if (done_) return;
    done_ = true;
    parent()->add<Label>("added while painting");
  }

 private:
  bool done_ = false;
};

// Replaces another label's text while it paints.
class PaintRetexts : public Box {
 public:
  PaintRetexts() : Box({60, 10}) {}
  void paint(Painter&) override {
    if (target)
      target->set_text(
          "an entirely different and also rather long text, so it is not stored inline");
  }
  Ref<Label> target;
};

// Paints solid red, except the first time, when it throws before drawing anything.
class ThrowsOnce : public Box {
 public:
  ThrowsOnce() : Box({60, 30}) {}
  void paint(Painter& p) override {
    if (!thrown_) {
      thrown_ = true;
      throw std::runtime_error("paint failed once");
    }
    p.fill_rect(bounds(), Color::hex(0xFF0000));
  }

 private:
  bool thrown_ = false;
};

font_builder::Bytes triangle(int size) {
  return font_builder::simple_glyph({{0, 0}, {size, 0}, {size / 2, size}});
}

}  // namespace

TEST(review2_a_cancel_handler_cannot_free_its_widget_mid_cancel) {
  // set_enabled() runs outside any event here; the cancel it sends must still not let a
  // nested loop destroy the widget while cancel_press() is using it.
  App app(headless());
  auto win = app.open_window({"t", {200, 100}});
  testing::Driver* driver = nullptr;
  auto w = win->add<Column>(StackOptions{.padding = Insets::all(10)})->add<CancelThenNest>(&driver);
  testing::Driver d(win);
  driver = &d;
  d.press(w->bounds().center());
  w->set_enabled(false);
  d.idle();
  CHECK(!w);
}

TEST(review2_a_paint_that_pumps_a_frame_does_not_draw_inside_itself) {
  App app(headless());
  auto win = app.open_window({"t", {300, 200}});
  testing::Driver d(win);
  testing::Driver* driver = &d;
  auto col = win->add<Column>(StackOptions{.spacing = 20, .padding = Insets::all(10)});
  auto other = col->add<Box>(Size{60, 30});
  col->add<PaintThenNest>(&driver, other);
  const int before = d.frames_drawn();
  d.frame();  // paints, invalidates the other box and pumps a nested frame
  CHECK_EQ(d.frames_drawn(), before + 1);  // the nested frame was not drawn inside this one
  CHECK(d.frame_requested());              // but it was kept for later
  d.frame();
  CHECK_EQ(d.frames_drawn(), before + 2);
}

TEST(review2_adding_a_widget_while_painting_is_a_contract_violation) {
  ExpectContracts expect;
  App app(headless());
  auto win = app.open_window({"t", {200, 100}});
  win->add<Column>()->add<PaintAdds>();
  testing::Driver d(win);
  CHECK(throws_contract([&] { app.run(); }));  // the violation inside the frame comes out of run()
}

TEST(review2_changing_a_wrapped_label_while_painting_reads_no_freed_text) {
  // The painter comes before the label, so in one frame the label is laid out (its lines cut
  // from the old text), then its text is replaced, then it paints.
  App app(headless());
  auto win = app.open_window({"t", {300, 200}});
  auto col = win->add<Column>();
  auto painter = col->add<PaintRetexts>();
  painter->target = col->add<Label>(
      "the original text, long enough to live on the heap and to wrap", LabelOptions{.wrap = true});
  testing::Driver d(win);
  d.frame();
  CHECK(painter->target);
}

TEST(review2_posted_work_that_keeps_coming_does_not_starve_frames) {
  App app(headless());
  auto win = app.open_window({"t", {200, 100}});
  win->add<Column>()->add<ProgressBar>()->set_indeterminate(true);  // always wants frames
  testing::Driver d(win);
  int runs = 0, frames_between = 0, last_frames = d.frames_drawn();
  Callback<void()>* again = nullptr;
  Callback<void()> step = [&] {
    ++runs;
    if (d.frames_drawn() != last_frames) ++frames_between;
    last_frames = d.frames_drawn();
    if (runs < 100)
      app.post([&] { (*again)(); });  // the continuation of a long job
    else
      app.quit();
  };
  again = &step;
  app.post([&] { step(); });
  app.run();
  CHECK_EQ(runs, 100);
  CHECK(frames_between > 0);  // frames ran between the pieces, not only after them all
}

TEST(review2_a_press_that_disables_its_widget_is_cancelled) {
  App app(headless());
  auto win = app.open_window({"t", {300, 100}});
  auto slider = win->add<Column>(StackOptions{.padding = Insets::all(20)})
                    ->add<Slider>(SliderOptions{.min = 0, .max = 100, .value = 50});
  slider->on_change([slider](float) { slider->set_enabled(false); });
  testing::Driver d(win);
  const Rect b = slider->bounds();
  d.press({b.x + 8, b.center().y});  // the press moves the value, and its callback disables it
  const float after_press = slider->value();
  slider->set_enabled(true);
  d.move({b.right() - 8, b.center().y});
  CHECK_EQ(slider->value(), after_press);  // the drag does not resume
  d.release({b.right() - 8, b.center().y});
}

TEST(review2_repeated_kern_records_cost_no_memory) {
  using namespace font_builder;
  Bytes bytes = font({{}, triangle(100)}, {{"cmap", cmap12({{65, 65, 1}})},
                                           {"GPOS", gpos_repeated_feature(1000, 1000)}});
  alloc_count::Scope scope;
  const detail::Font f(std::move(bytes));
  CHECK(scope.allocated_bytes() <
        512 * 1024);  // a million references to nothing must not be stored
  CHECK_EQ(f.kerning(1, 1), 0);
}

TEST(review2_a_giant_glyph_is_not_rasterized) {
  // Units-per-em 16 and a triangle 4000 units wide: at 14 px it would be 3500 px square.
  using namespace font_builder;
  const detail::Font f(font({{}, triangle(4000)}, {{"cmap", cmap12({{65, 65, 1}})}}, 16));
  detail::GlyphCache cache;
  alloc_count::Scope scope;
  const detail::MaskView m = cache.get(f, 1, 14.0f, 0, 1.0f);
  CHECK(scope.allocated_bytes() < 8 * 1024 * 1024);
  CHECK(m.empty());
}

TEST(review2_a_paint_exception_does_not_lose_damage) {
  App app(headless(&theme_light()));
  auto win = app.open_window({"t", {200, 100}});
  auto col = win->add<Column>(StackOptions{.spacing = 10, .padding = Insets::all(10)});
  auto thrower = col->add<ThrowsOnce>();
  auto other = col->add<Box>(Size{20, 10});
  testing::Driver d(win);  // the first frame throws inside ThrowsOnce
  other->invalidate();     // a small, unrelated change afterwards
  d.frame();
  d.frame();
  const Point c = thrower->bounds().center();
  CHECK_EQ(d.pixel(static_cast<int>(c.x), static_cast<int>(c.y)) & 0xFFFFFF, 0xFF0000u);
}

TEST(review2_focus_skips_widgets_inside_hidden_containers) {
  App app(headless());
  auto win = app.open_window({"t", {200, 100}});
  auto col = win->add<Column>();
  auto button = col->add<Button>("inside");
  testing::Driver d(win);
  col->set_visible(false);
  button->focus();
  CHECK(!button->has_focus());
}

TEST(review2_a_spanning_child_does_not_starve_a_stretch_column) {
  App app(headless());
  auto win = app.open_window({"t", {200, 100}});
  auto grid = win->add<Grid>(
      GridOptions{.columns = {GridTrack::fit(), GridTrack::stretch()}, .column_spacing = 0});
  grid->add<Box>(Size{20, 10});
  auto field = grid->add<Box>(Size{10, 10});
  auto header = grid->add<Box>(Size{200, 10});
  header->set_column_span(2);
  testing::Driver(win).frame();
  CHECK_NEAR(field->bounds().width, 180.0f, 0.01f);  // the fit column keeps its own width
}

TEST(review2_kerning_beyond_16_bits_is_the_same_every_time) {
  using namespace font_builder;
  const detail::Font f(font({{}, triangle(100), triangle(100)},
                            {{"cmap", cmap12({{65, 65, 1}, {86, 86, 2}})},
                             {"GPOS", gpos_kern({{1, 2, -20000}, {1, 2, -20000}})}}));
  const int first = f.kerning(1, 2);
  CHECK_EQ(f.kerning(1, 2), first);
  CHECK(first < 0);
}

TEST(review2_another_button_does_not_end_the_press) {
  App app(headless());
  auto win = app.open_window({"t", {200, 100}});
  auto button = win->add<Column>(StackOptions{.padding = Insets::all(10)})->add<Button>("hold");
  int clicks = 0;
  button->on_click([&] { ++clicks; });
  testing::Driver d(win);
  const Point p = button->bounds().center();
  d.press(p);
  d.press(p, MouseButton::right);
  d.release(p, MouseButton::right);
  d.release(p);
  CHECK_EQ(clicks, 1);
}

TEST(review2_a_partial_repaint_keeps_a_nested_cards_shadow) {
  App app(headless());
  auto win = app.open_window({"t", {300, 300}});
  auto col = win->add<Column>(
      StackOptions{.spacing = 20, .padding = Insets::all(20), .align = Align::start});
  auto card = col->add<Row>()->add<Card>();
  card->add<Box>(Size{100, 40});
  auto below = col->add<Box>(Size{100, 10});
  testing::Driver d(win);
  const Rect cb = card->bounds();
  const int x = static_cast<int>(cb.center().x), y = static_cast<int>(cb.bottom() + 17);
  const uint32_t full = d.pixel(x, y);
  below->invalidate();  // its repaint area reaches into the card's shadow
  d.frame();
  CHECK_EQ(d.pixel(x, y), full);
}

TEST(review2_invalid_utf8_forms_are_replaced) {
  for (const char* bad : {"\xC0\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80", "\xE0\x80\x80"}) {
    const std::string s = bad;
    size_t i = 0;
    CHECK_EQ(static_cast<uint32_t>(detail::next_codepoint(s, i)), 0xFFFDu);
  }
  const std::string fine = "\xF4\x8F\xBF\xBF";  // U+10FFFF, the last one there is
  size_t i = 0;
  CHECK_EQ(static_cast<uint32_t>(detail::next_codepoint(fine, i)), 0x10FFFFu);
}

TEST(review2_app_calls_from_another_thread_are_contract_violations) {
  App app(headless());
  ExpectContracts expect;
  bool stats = false, theme = false;
  std::thread t([&] {
    stats = throws_contract([&] { (void)app.memory_stats(); });
    theme = throws_contract([&] { app.set_theme(theme_light()); });
  });
  t.join();
  CHECK(stats);
  CHECK(theme);
}
