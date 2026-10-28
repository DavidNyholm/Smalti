// Regressions for defects found in review round 1. Each test reproduces the failure scenario
// the review described; several only fail loudly under AddressSanitizer (the linux-asan preset).
#include <memory>

#include "check.hpp"
#include "core/state.hpp"
#include "font_builder.hpp"
#include "render/canvas.hpp"
#include "render/text.hpp"
#include "support.hpp"

using namespace smalti;
using namespace std::chrono_literals;

namespace {

// A container that builds its own children in its constructor, as composite widgets do.
class Panel : public Column {
 public:
  Panel() { inner = add<Button>("inner"); }
  Ref<Button> inner;
};

// A widget that invalidates other widgets while it paints.
class NoisyPainter : public Widget {
 public:
  explicit NoisyPainter(std::vector<Ref<Widget>>* targets) : targets_(targets) {}
  Size measure(Size) override { return {40, 20}; }
  void paint(Painter&) override {
    if (paints_++ > 2) return;
    for (auto& t : *targets_) t->invalidate();
  }

 private:
  std::vector<Ref<Widget>>* targets_;
  int paints_ = 0;
};

}  // namespace

TEST(review_shadow_cache_keys_with_different_sizes_do_not_collide) {
  // Blur 1.0 and 1.001 quantise to the same cache key but once needed different tile sizes:
  // the second read past the end of the first's tile.
  detail::Bitmap a(200, 200), b(200, 200);
  detail::Canvas ca, cb;
  ca.set_target(a.view());
  cb.set_target(b.view());
  ca.clear(Color::hex(0xFFFFFF));
  cb.clear(Color::hex(0xFFFFFF));
  ca.shadow({40, 40, 100, 100}, 2, 1.0f, {0, 0, 0, 0.8f}, false);
  ca.shadow({40, 40, 100, 100}, 2, 1.001f, {0, 0, 0, 0.8f}, false);
  cb.shadow({40, 40, 100, 100}, 2, 1.0f, {0, 0, 0, 0.8f}, false);
  cb.shadow({40.0001f, 40.0001f, 100, 100}, 2, 1.001f, {0, 0, 0, 0.8f}, false);  // direct path
  int worst = 0;
  for (size_t i = 0; i < a.pixels.size(); ++i) {
    worst = std::max(worst, std::abs(static_cast<int>(a.pixels[i] & 0xFF) -
                                     static_cast<int>(b.pixels[i] & 0xFF)));
  }
  CHECK(worst <= 2);
}

TEST(review_grid_with_no_columns_lays_out) {
  App app(headless());
  auto win = app.open_window({"t", {200, 100}});
  auto grid = win->add<Grid>(GridOptions{.columns = {}});
  auto label = grid->add<Label>("only child");
  testing::Driver(win).frame();
  CHECK(label->bounds().width > 0);
}

TEST(review_callback_may_replace_itself) {
  App app(headless());
  auto win = app.open_window({"t", {200, 100}});
  auto button = win->add<Column>()->add<Button>("again");
  int seen = 0;
  button->on_click([button, &seen, token = std::make_unique<int>(42)] {
    button->on_click([] {});  // replaces the running callback
    seen = *token;            // the running callback's capture must still be alive
  });
  testing::Driver d(win);
  d.click(button);
  CHECK_EQ(seen, 42);
}

TEST(review_removal_waits_for_the_outermost_event) {
  // A callback removes its widget, then runs a nested event loop (as a native message box
  // would). The nested loop must not destroy the widget while its handler is still running.
  App app(headless());
  auto win = app.open_window({"t", {200, 100}});
  auto button = win->add<Column>()->add<Button>("remove me");
  testing::Driver d(win);
  button->on_click([button, &d] {
    button->remove();
    d.frame();  // the nested loop
  });
  d.click(button);
  d.frame();
  CHECK(!button);
}

TEST(review_invalidate_during_paint_is_kept_for_the_next_frame) {
  App app(headless());
  auto win = app.open_window({"t", {400, 400}});
  auto col = win->add<Column>(StackOptions{.spacing = 4});
  std::vector<Ref<Widget>> targets;
  col->add<NoisyPainter>(&targets);
  for (int i = 0; i < 20; ++i) targets.push_back(col->add<Label>("target"));
  testing::Driver d(win);
  d.frame();
  // The invalidations made while painting must still be pending: another frame is due.
  CHECK(d.frame_requested());
  d.advance(100ms);
}

TEST(review_children_built_in_a_constructor_belong_to_the_window) {
  App app(headless());
  auto win = app.open_window({"t", {300, 100}});
  auto panel = win->add<Panel>();
  testing::Driver d(win);
  CHECK(panel->inner->window() == win);
  d.advance(1s);
  d.move(panel->inner->bounds().center());
  CHECK(d.frame_requested());  // the hover animation reached the window
}

TEST(review_hidden_widgets_lose_focus_and_ignore_keys) {
  App app(headless());
  auto win = app.open_window({"t", {300, 100}});
  auto col = win->add<Column>(StackOptions{.padding = Insets::all(10)});
  auto button = col->add<Button>("hidden soon");
  int clicks = 0;
  button->on_click([&] { ++clicks; });
  testing::Driver d(win);
  d.key(Key::tab);
  CHECK(button->has_focus());
  col->set_visible(false);  // a hidden ancestor hides it too
  d.key(Key::space);
  CHECK_EQ(clicks, 0);
  CHECK(!button->has_focus());
}

TEST(review_disabling_a_pressed_control_cancels_the_press) {
  App app(headless());
  auto win = app.open_window({"t", {300, 100}});
  auto check =
      win->add<Column>(StackOptions{.padding = Insets::all(10)})->add<CheckBox>("press me");
  testing::Driver d(win);
  const Point p = check->bounds().center();
  d.press(p);
  check->set_enabled(false);
  d.release(p);
  CHECK(!check->checked());
}

TEST(review_slider_with_reversed_bounds) {
  App app(headless());
  auto win = app.open_window({"t", {300, 100}});
  auto slider = win->add<Column>()->add<Slider>(SliderOptions{.min = 100, .max = 0, .value = 50});
  CHECK_EQ(slider->value(), 50.0f);
  testing::Driver d(win);
  d.key(Key::tab);
  d.key(Key::home);
  CHECK_EQ(slider->value(), 0.0f);
}

TEST(review_radio_group_stays_reachable_when_the_selection_is_hidden) {
  App app(headless());
  auto win = app.open_window({"t", {300, 200}});
  auto group = win->add<Column>();
  auto first = group->add<RadioButton>("first", true);
  auto second = group->add<RadioButton>("second");
  first->set_visible(false);
  testing::Driver d(win);
  d.key(Key::tab);
  CHECK(second->has_focus());
}

TEST(review_grid_span_contributes_its_width) {
  App app(headless());
  auto win = app.open_window({"t", {400, 100}});
  auto grid = win->add<Column>(StackOptions{.align = Align::start})
                  ->add<Grid>(GridOptions{.columns = {GridTrack::fit(), GridTrack::fit()},
                                          .column_spacing = 0});
  auto wide = grid->add<Label>("a label that is wide");
  wide->set_column_span(2);
  testing::Driver(win).frame();
  CHECK(wide->bounds().width >= wide->measure({1e9f, 1e9f}).width);
}

TEST(review_an_empty_window_still_draws_its_background) {
  App app(headless(&theme_light()));
  auto win = app.open_window({"t", {100, 100}});
  testing::Driver d(win);
  CHECK_EQ(d.pixel(50, 50) & 0xFFFFFF, 0xFAFAFBu);
}

TEST(review_glyph_cache_bounds_empty_glyph_entries) {
  const detail::FontSet fonts = detail::load_embedded_fonts();
  detail::GlyphCache cache(64 * 1024);
  const uint16_t space = fonts.regular->glyph_index(' ');
  for (int i = 0; i < 20000; ++i)
    cache.get(*fonts.regular, space, 8.0f + static_cast<float>(i) * 0.02f, 0, 1.0f);
  CHECK(cache.entries() < 5000);
}

TEST(review_callback_accepts_callables_that_return_a_value) {
  int calls = 0;
  Callback<void()> discards = [&calls] { return ++calls; };
  discards();
  CHECK_EQ(calls, 1);
}

namespace {

font_builder::Bytes triangle() { return font_builder::simple_glyph({{0, 0}, {100, 0}, {50, 100}}); }

}  // namespace

TEST(review_composite_glyph_bombs_are_refused) {
  // Every level holds 16 copies of the level below: 16^4 triangles from a font of a few
  // hundred bytes, all within the nesting limit. (Real composites have a handful of parts.)
  using namespace font_builder;
  std::vector<Bytes> glyphs{{}, triangle()};
  for (int level = 0; level < 4; ++level)
    glyphs.push_back(composite_glyph(static_cast<uint16_t>(glyphs.size() - 1), 16));
  const detail::Font f(font(glyphs, {{"cmap", cmap12({{65, 65, 1}})}}));
  detail::Path p;
  f.outline(1, p);
  CHECK(!p.empty());  // the triangle itself is fine
  f.outline(static_cast<uint16_t>(glyphs.size() - 1), p);
  CHECK(p.verbs().size() < 4096);
}

TEST(review_cmap12_group_count_is_not_trusted) {
  using namespace font_builder;
  const detail::Font honest(font({{}, triangle()}, {{"cmap", cmap12({{65, 65, 1}})}}));
  CHECK_EQ(honest.glyph_index('A'), 1);
  const detail::Font lying(font({{}, triangle()}, {{"cmap", cmap12({{65, 65, 1}}, 0x80000000u)}}));
  CHECK_EQ(lying.glyph_index('A'), 0);  // missing, without signed overflow on the way
}

TEST(review_kerning_adds_up_across_lookups) {
  using namespace font_builder;
  const detail::Font f(
      font({{}, triangle(), triangle()}, {{"cmap", cmap12({{65, 65, 1}, {86, 86, 2}})},
                                          {"GPOS", gpos_kern({{1, 2, -20}, {1, 2, -30}})}}));
  CHECK_EQ(f.kerning(1, 2), -50);
}

TEST(review_a_small_window_is_not_held_to_the_default_minimum) {
  App app(headless());
  auto win = app.open_window({"t", {380, 220}});
  CHECK_EQ(win->state().min_size.height, 220.0f);
  CHECK_EQ(win->state().min_size.width, 320.0f);  // the default, where it fits
}

TEST(review_run_is_not_reentrant) {
  ExpectContracts expect;
  App app(headless());
  app.open_window();
  app.post([&app] { app.run(); });
  CHECK(throws_contract([&] { app.run(); }));
}

TEST(review_removing_a_pressed_widget_ends_the_press) {
  App app(headless());
  auto win = app.open_window({"t", {300, 100}});
  auto button = win->add<Column>(StackOptions{.padding = Insets::all(10)})->add<Button>("going");
  int clicks = 0;
  button->on_click([&] { ++clicks; });
  testing::Driver d(win);
  const Point p = button->bounds().center();
  d.press(p);
  button->remove();
  d.idle();
  CHECK(!button);
  d.release(p);
  d.move({p.x + 1, p.y});
  CHECK_EQ(clicks, 0);
}
