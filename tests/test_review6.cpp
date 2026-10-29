// Regression tests for the findings of review round 6 that apply to 0.1.1. Round 6 reviewed the
// theme work for 0.2 in beta; these defects were older than it. docs/How-it-is-checked.md lists
// every finding.
#include <chrono>
#include <cmath>
#include <limits>
#include <string>

#include "check.hpp"
#include "platform/platform.hpp"
#include "support.hpp"

using namespace smalti;
using namespace std::chrono_literals;

namespace {

// A widget painting one rectangle in whatever colour it is given.
class Swatch : public Widget {
 public:
  Color color;
  Size measure(Size) override { return {20, 20}; }
  void paint(Painter& p) override { p.fill_rect(bounds(), color); }
};

}  // namespace

// Finding 4: an Xft.dpi of "inf" passed the dpi > 0 check, and an infinite scale reached
// lround and the conversion of window sizes to int.
TEST(review6_dpi_and_window_sizes_are_trusted_only_when_sane) {
  CHECK_EQ(platform::scale_from_dpi_text("96"), 1.0f);
  CHECK_EQ(platform::scale_from_dpi_text("192"), 2.0f);
  CHECK_EQ(platform::scale_from_dpi_text("120 \n"), 1.25f);
  for (const char* bad : {"inf", "nan", "1e30", "-96", "0", "", "abc", "96px", "4"}) {
    CHECK_EQ(platform::scale_from_dpi_text(bad), 1.0f);
  }
  CHECK_EQ(platform::scale_from_dpi_text(nullptr), 1.0f);
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  CHECK_EQ(platform::window_pixels(100, 1.5f), 150);
  CHECK_EQ(platform::window_pixels(nan, 1), 1);
  CHECK_EQ(platform::window_pixels(-5, 1), 1);
  CHECK_EQ(platform::window_pixels(1e30f, 1), platform::kMaxWindowPixels);
  CHECK_EQ(platform::window_pixels(100, inf), platform::kMaxWindowPixels);
  // A window asked for at an absurd size opens at the largest size instead.
  App app(headless());
  auto win = app.open_window({"t", {nan, inf}});
  testing::Driver d(win);
  CHECK_EQ(d.width(), 1);
  CHECK_EQ(d.height(), platform::kMaxWindowPixels);
}

// Finding 5: a colour outside 0..1 reached the canvas, which packed the overflowing channels
// into a wrong colour. Whatever a custom widget passes, the nearest storable colour is drawn.
TEST(review6_colours_past_their_range_draw_clamped) {
  App app(headless());
  auto win = app.open_window({"t", {100, 100}});
  auto swatch =
      win->add<Column>(StackOptions{0, Insets::all(10), Align::start, Align::start})->add<Swatch>();
  swatch->color = {2, -1, 0.5f, 1.5f};
  testing::Driver d(win);
  const Rect b = swatch->bounds();
  const int x = static_cast<int>(b.center().x), y = static_cast<int>(b.center().y);
  CHECK_EQ(d.pixel(x, y) & 0xFFFFFF, 0xFF0080u);
  swatch->color = {std::numeric_limits<float>::quiet_NaN(), 1, 1, 1};
  swatch->invalidate();
  d.frame();
  CHECK_EQ(d.pixel(x, y) & 0xFFFFFF, 0x00FFFFu);
}

// Finding 5 as a theme meets it: a secondary button's hover tint is the theme's hover colour,
// half again as opaque, so an opaque hover colour gave an alpha of 1.5.
TEST(review6_an_opaque_hover_colour_tints_as_itself) {
  Theme th = theme_dark();
  th.hover = Color::hex(0xFFFFFF);
  App app(headless(&th));
  auto win = app.open_window({"t", {200, 100}});
  auto button = win->add<Column>(StackOptions{0, Insets::all(20), Align::start, Align::start})
                    ->add<Button>("Hover", ButtonVariant::secondary);
  testing::Driver d(win);
  d.move(button->bounds().center());
  d.advance(1s);
  const Rect b = button->bounds();
  CHECK_EQ(d.pixel(static_cast<int>(b.x + 3), static_cast<int>(b.center().y)) & 0xFFFFFF,
           0xFFFFFFu);
}
