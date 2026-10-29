// Regressions for defects found in review round 4. Some only fail loudly under the sanitizers
// (the linux-asan preset).
#include <stdexcept>
#include <vector>

#include "check.hpp"
#include "core/state.hpp"
#include "render/canvas.hpp"
#include "support.hpp"

using namespace smalti;
using namespace std::chrono_literals;

namespace {

class Box : public Widget {
 public:
  explicit Box(Size size) : size_(size) {}
  Size measure(Size) override { return size_; }

 private:
  Size size_;
};

// A container that takes presses, and counts them.
class Grabber : public Column {
 public:
  bool on_mouse(const MouseEvent& e) override {
    if (e.kind == MouseEvent::Kind::press) ++presses;
    return e.kind != MouseEvent::Kind::move;
  }
  int presses = 0;
};

// Declines presses, but the first time pumps the release from inside its press handler, as a
// handler that runs a nested loop might.
class ReleasesInsidePress : public Box {
 public:
  explicit ReleasesInsidePress(testing::Driver** driver) : Box({60, 30}), driver_(driver) {}
  bool on_mouse(const MouseEvent& e) override {
    if (e.kind == MouseEvent::Kind::press && !done_) {
      done_ = true;
      (*driver_)->release(e.position);
    }
    return false;
  }

 private:
  testing::Driver** driver_;
  bool done_ = false;
};

// Paints what it is given to paint.
class Painting : public Box {
 public:
  explicit Painting(Callback<void(Painter&, Rect)> fn) : Box({100, 40}), fn_(std::move(fn)) {}
  void paint(Painter& p) override { fn_(p, bounds()); }

 private:
  Callback<void(Painter&, Rect)> fn_;
};

// Records the hover changes it is told about; on losing hover it hides `hide_on_leave`.
class HoverProbe : public Box {
 public:
  HoverProbe() : Box({60, 30}) {}
  void on_hover_changed(bool hovered) override {
    told.push_back(hovered);
    if (!hovered && hide_on_leave) hide_on_leave->set_visible(false);
  }
  std::vector<bool> told;
  Ref<Widget> hide_on_leave;
};

}  // namespace

TEST(review4_a_press_released_inside_its_handler_does_not_bubble_on) {
  App app(headless());
  auto win = app.open_window({"t", {200, 100}});
  testing::Driver d(win);
  testing::Driver* driver = &d;
  auto parent = win->add<Grabber>();
  auto child = parent->add<ReleasesInsidePress>(&driver);
  d.frame();
  d.press(child->bounds().center());
  CHECK_EQ(parent->presses, 0);  // the button is already up: nobody may take this press now
}

TEST(review4_spans_in_several_rows_share_columns_with_stretch) {
  App app(headless());
  auto win = app.open_window({"t", {200, 200}});
  auto grid = win->add<Grid>(
      GridOptions{.columns = {GridTrack::fit(), GridTrack::stretch(), GridTrack::stretch()},
                  .column_spacing = 0});
  grid->add<Box>(Size{20, 10});
  grid->add<Box>(Size{10, 10});
  grid->add<Box>(Size{10, 10});
  std::vector<Ref<Box>> spans, lasts;
  for (int row = 0; row < 3; ++row) {
    spans.push_back(grid->add<Box>(Size{150, 10}));
    spans.back()->set_column_span(2);
    lasts.push_back(grid->add<Box>(Size{10, 10}));
  }
  testing::Driver(win).frame();
  for (auto& s : spans) CHECK_NEAR(s->bounds().width, 150.0f, 0.5f);
  for (auto& l : lasts)
    CHECK(l->bounds().right() <= 200.5f);  // 100, 50, 50 fits; 260, 0, 0 does not
}

TEST(review4_two_spans_over_different_stretch_columns_both_fit) {
  App app(headless());
  auto win = app.open_window({"t", {300, 100}});
  auto grid = win->add<Grid>(
      GridOptions{.columns = {GridTrack::fit(), GridTrack::stretch(), GridTrack::fit(),
                              GridTrack::stretch(), GridTrack::stretch()},
                  .column_spacing = 0});
  auto a = grid->add<Box>(Size{140, 10});
  a->set_column_span(2);
  auto b = grid->add<Box>(Size{140, 10});
  b->set_column_span(2);
  grid->add<Box>(Size{10, 10});
  testing::Driver(win).frame();
  CHECK(a->bounds().width >= 139.5f);  // 120 + 20 each: it can be done
  CHECK(b->bounds().width >= 139.5f);
}

TEST(review4_a_rectangle_far_off_screen_writes_nothing) {
  App app(headless());
  auto win = app.open_window({"t", {200, 100}});
  win->add<Column>()->add<Painting>([](Painter& p, Rect) {
    p.fill_rect({2147483520.0f, 0, 128.0f, 10}, Color::hex(0xFF0000));
    p.fill_rect({-2147483520.0f, 0, 128.0f, 10}, Color::hex(0xFF0000));
    p.fill_rect({0, 0, 3.0e9f, 3.0e9f}, Color::hex(0x00FF00, 0.5f));  // larger than any int
  });
  testing::Driver d(win);
  const uint32_t px = d.pixel(150, 90);
  CHECK((px >> 16 & 0xFF) < 0x40);  // neither far-off red rectangle landed here
  CHECK((px >> 8 & 0xFF) > 0x60);   // the oversized green one, clamped, still covers the window
}

TEST(review4_a_nearly_flat_polyline_draws_without_invalid_arithmetic) {
  App app(headless());
  auto win = app.open_window({"t", {200, 100}});
  win->add<Column>()->add<Painting>([](Painter& p, Rect b) {
    const Point flat[] = {{b.x, b.y}, {b.x + 100, b.y + 1e-37f}};
    p.polyline(flat, 1e-37f, Color::hex(0xFF0000));
  });
  testing::Driver d(win);
  CHECK(d.frames_drawn() >= 1);
}

TEST(review4_the_shadow_cache_has_a_budget) {
  detail::Bitmap target(500, 500);
  detail::Canvas canvas;
  canvas.set_target(target.view());
  // Every blur from 1 to 16 px in 1/16 px steps: hundreds of different corner tiles.
  for (int n = 16; n <= 256; ++n)
    canvas.shadow({50, 50, 300, 300}, 12, static_cast<float>(n) / 16, {0, 0, 0, 0.5f}, false);
  CHECK(canvas.cache_bytes() <= (1u << 20) + (64u << 10));
}

TEST(review4_a_callback_exception_comes_out_of_the_test_driver) {
  App app(headless());
  auto win = app.open_window({"t", {200, 100}});
  auto button = win->add<Column>(StackOptions{.padding = Insets::all(10)})->add<Button>("fails");
  bool entered = false;
  button->on_click([&] {
    entered = true;
    throw std::runtime_error("click failed");
  });
  testing::Driver d(win);
  bool threw = false;
  try {
    d.click(button);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  CHECK(entered);
  CHECK(threw);  // a test must not pass while a callback failed
}

TEST(review4_hover_taken_back_by_the_old_widget_sends_no_stale_gain) {
  App app(headless());
  auto win = app.open_window({"t", {300, 100}});
  auto row = win->add<Row>(StackOptions{.spacing = 10});
  auto a = row->add<HoverProbe>();
  auto b = row->add<HoverProbe>();
  a->hide_on_leave = b;
  testing::Driver d(win);
  d.move(a->bounds().center());
  d.move(b->bounds().center());  // a's leave hides b on the way
  for (bool hovered : b->told) CHECK(!hovered);
}

TEST(review4_a_zero_width_border_is_one_device_pixel) {
  App app(headless());
  auto win = app.open_window({"t", {200, 100}});
  auto painter = win->add<Column>(StackOptions{.padding = Insets::all(20)})
                     ->add<Painting>([](Painter& p, Rect b) {
                       p.stroke_rounded_rect(b, 0, 0, Color::hex(0xFF0000));
                     });
  testing::Driver d(win);
  d.resize({200, 100}, 2.0f);
  d.frame();
  const Rect b = painter->bounds();
  const int y = static_cast<int>(b.center().y * 2);
  const int left = static_cast<int>(b.x * 2);
  int red = 0;
  for (int x = left - 2; x < left + 6; ++x) red += (d.pixel(x, y) >> 16 & 0xFF) > 128 ? 1 : 0;
  CHECK_EQ(red, 1);
}

TEST(review4_two_radios_selected_at_the_start_settle_on_a_click) {
  App app(headless());
  auto win = app.open_window({"t", {200, 100}});
  auto group = win->add<Column>(StackOptions{.padding = Insets::all(10)});
  auto a = group->add<RadioButton>("a", true);
  auto b = group->add<RadioButton>("b", true);
  testing::Driver d(win);
  d.click(b);
  CHECK(b->selected());
  CHECK(!a->selected());
}

TEST(review4_space_on_a_button_shows_the_press) {
  App app(headless(&theme_light()));
  auto win = app.open_window({"t", {200, 100}});
  auto button = win->add<Column>(StackOptions{.padding = Insets::all(20), .align = Align::start})
                    ->add<Button>("OK", ButtonVariant::primary);
  testing::Driver d(win);
  d.key(Key::tab);
  d.advance(1s);
  const Rect b = button->bounds();
  const int x = static_cast<int>(b.x + 3), y = static_cast<int>(b.center().y);
  const uint32_t settled = d.pixel(x, y);
  d.key(Key::space);
  d.frame();  // the same moment: the press is at its deepest
  CHECK(d.pixel(x, y) != settled);
}

TEST(review4_a_span_wider_than_the_grid_does_not_starve_a_stretch_column) {
  // The header asks for more than the whole grid and covers its only stretch column: no growth
  // of the fit column can help it, so the field beside the label keeps its share.
  App app(headless());
  auto win = app.open_window({"t", {200, 100}});
  auto grid = win->add<Grid>(
      GridOptions{.columns = {GridTrack::fit(), GridTrack::stretch()}, .column_spacing = 0});
  grid->add<Box>(Size{20, 10});
  auto field = grid->add<Box>(Size{10, 10});
  auto header = grid->add<Box>(Size{250, 10});
  header->set_column_span(2);
  testing::Driver(win).frame();
  CHECK_NEAR(field->bounds().width, 180.0f, 0.5f);
}
