// Regression tests for review round 6 (theme files and widget styles); one per finding that can
// be reproduced without a screen. docs/How-it-is-checked.md lists every finding.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>

#include "check.hpp"
#include "platform/platform.hpp"
#include "support.hpp"
#include "theme/jsonc.hpp"
#include "theme/widget_styles.hpp"

#if !defined(_WIN32)
#include <unistd.h>

#include <sys/stat.h>
#endif

using namespace smalti;
using namespace std::chrono_literals;

namespace {

bool contains(std::string_view s, std::string_view part) {
  return s.find(part) != std::string_view::npos;
}

// A widget painting one rectangle in whatever colour it is given.
class Swatch : public Widget {
 public:
  Color color;
  Size measure(Size) override { return {20, 20}; }
  void paint(Painter& p) override { p.fill_rect(bounds(), color); }
};

}  // namespace

// Finding 1: unknown properties were remembered for the duplicate check before they were
// validated, so a block of 65,000 of them made two billion comparisons.
TEST(review6_many_unknown_widget_properties_cost_little) {
  constexpr int kNames = 65000;
  std::string text = R"({"widgets": {"Button": {)";
  for (int i = 0; i < kNames; ++i) {
    char name[16];
    std::snprintf(name, sizeof name, "\"x%05d\":0,", i);
    text += name;
  }
  text += "}}}";
  CHECK(text.size() < detail::kMaxJsonBytes);
  const auto start = std::chrono::steady_clock::now();
  const ThemeLoad r = parse_theme(text);
  const auto took = std::chrono::steady_clock::now() - start;
  CHECK_EQ(r.errors.size(), size_t{101});  // 100 reported, then the count of the rest
  // Linear work takes well under a second even unoptimised; the quadratic kind took minutes.
  constexpr auto kGenerous = 10s;
  CHECK(took < kGenerous);
}

// Finding 2: the loader measured a file and then opened it again, so a FIFO put in its place
// blocked the open forever, and a file that grew was read only in part.
TEST(review6_load_theme_refuses_a_pipe_or_device_without_waiting) {
#if defined(_WIN32)
  const ThemeLoad r = load_theme("NUL");  // a device: reading it would never end
#else
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / ("smalti_review6_" + std::to_string(::getpid()));
  std::filesystem::create_directories(dir);
  const std::filesystem::path fifo = dir / "theme.jsonc";
  CHECK(mkfifo(fifo.c_str(), 0600) == 0);
  const ThemeLoad r = load_theme(fifo);  // with no writer, a blocking open would never return
  std::filesystem::remove_all(dir);
#endif
  CHECK_EQ(r.errors.size(), size_t{1});
  CHECK(!r.errors.empty() && contains(r.errors[0].message, "not a regular file"));
}

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

// Finding 5: an opaque state.hover made the ghost button's hover tint 150% opaque, and the
// canvas packed the overflowing channels into a wrong colour.
TEST(review6_colours_past_their_range_draw_clamped) {
  const ThemeLoad r = parse_theme(R"({"tokens": {"state.hover": "#ffffff"}})");
  CHECK(r.ok());
  const detail::WidgetStyles styles = detail::compute_widget_styles(r.theme);
  const Color hovered = styles
                            .get(detail::StyledWidget::button, 2)  // ghost
                            .color(detail::ButtonSlot::fill, {.hover = 1});
  CHECK(hovered.a <= 1.0f);
  // Whatever a custom widget passes, the canvas draws the nearest colour it can store.
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

// Finding 6: the switch's default hover knob mixed straight from the resting knob, so a
// transparent knob's own (invisible) colour showed under the pointer.
TEST(review6_a_transparent_switch_knob_hovers_without_its_hue) {
  const ThemeLoad r = parse_theme(R"({"widgets": {"Switch": {"knob": "#ff000000"}}})");
  CHECK(r.ok());
  const Color knob = detail::compute_widget_styles(r.theme)
                         .get(detail::StyledWidget::switch_toggle)
                         .color(detail::SwitchSlot::knob, {.hover = 1});
  const Color text = theme_dark().text;
  CHECK(std::fabs(knob.r - text.r) < 1e-5f && std::fabs(knob.g - text.g) < 1e-5f);
  CHECK(std::fabs(knob.a - 0.5f) < 1e-5f);
}

// Finding 7: a button kept size.control as its height whatever its font, so large text drew
// outside it, beyond what a redraw covered.
TEST(review6_a_button_is_as_tall_as_its_text) {
  const ThemeLoad r = parse_theme(R"({"tokens": {"font.body": 96, "size.control": 16}})");
  CHECK(r.ok());
  App app(headless(&r.theme));
  auto win = app.open_window({"t", {600, 300}});
  auto button = win->add<Column>(StackOptions{0, Insets::all(10), Align::start, Align::start})
                    ->add<Button>("H");
  testing::Driver d(win);
  CHECK(button->bounds().height >= 96);
}

// Finding 8: a loop through a token the file inherits was reported at the inherited token,
// inside the built-in theme's file.
TEST(review6_a_loop_through_the_base_is_reported_in_the_file) {
  const ThemeLoad r =
      parse_theme(R"({"tokens":{"surface.titlebar":"accent.soft","accent":"accent.soft"}})");
  CHECK_EQ(r.errors.size(), size_t{1});
  CHECK(!r.errors.empty() && r.errors[0].line == 1 && r.errors[0].column == 54);
  CHECK(!r.errors.empty() && contains(r.errors[0].message, "loop"));
}

// Finding 10: a lone carriage return did not end a line, so a // comment ran on over the
// next line and later positions were wrong.
TEST(review6_carriage_returns_end_lines) {
  CHECK(!detail::parse_jsonc("// a theme\r{}").error);
  detail::JsonParse p = detail::parse_jsonc("{\r\"x\": 1}");
  CHECK(!p.error);
  CHECK(!p.root.members.empty() && p.root.members[0].pos.line == 2 &&
        p.root.members[0].pos.column == 1);
  p = detail::parse_jsonc("{\r\r\n\"x\": 1}");  // a lone \r, then \r\n: two line ends
  CHECK(!p.root.members.empty() && p.root.members[0].pos.line == 3);
  const detail::JsonParse broken = detail::parse_jsonc("{\"a\": \"b\rc\"}");
  CHECK(broken.error && contains(broken.error->message, "not closed before the end of the line"));
}

// Finding 11: any other error stopped the loader before it looked for reference loops.
TEST(review6_loops_are_reported_with_other_errors) {
  const ThemeLoad r = parse_theme(R"({"tokens":{"accent":"accent","radius.control":101}})");
  CHECK_EQ(r.errors.size(), size_t{2});
  CHECK(r.errors.size() == 2 && r.errors[0].column == 21 && contains(r.errors[0].message, "loop"));
  CHECK(r.errors.size() == 2 && r.errors[1].column == 47 &&
        contains(r.errors[1].message, "from 0 to 100"));
}
