// Memory: churn returns to where it started, repaints allocate nothing once warm, budgets hold.
#include "check.hpp"
#include "support.hpp"

using namespace smalti;
using namespace std::chrono_literals;

namespace {

void build_controls(Ref<Column> col) {
  col->add<Label>("Settings", LabelOptions{TextRole::title});
  col->add<Button>("Save changes", ButtonVariant::primary);
  col->add<Button>("Cancel");
  col->add<CheckBox>("Dithering", true);
  col->add<Switch>("Live reload");
  col->add<RadioButton>("Daily", true);
  col->add<Slider>(SliderOptions{0, 1, 0.4f});
  col->add<ProgressBar>(0.6f);
  col->add<Separator>();
  auto card = col->add<Card>();
  card->add<Label>(
      "A card with a shadow and a wrapped label that needs more than one line to fit its width.",
      LabelOptions{TextRole::secondary, true});
}

}  // namespace

TEST(memory_widget_churn_returns_to_baseline) {
  App app(headless());
  auto win = app.open_window({"t", {400, 600}});
  testing::Driver d(win);
  const size_t baseline = app.memory_stats().widgets;
  for (int round = 0; round < 2000; ++round) {
    auto col = win->add<Column>(StackOptions{4, Insets::all(8), Align::stretch, Align::start});
    build_controls(col);
    if (round % 50 == 0) d.frame();  // lay out and paint some of them
    col->remove();
    d.idle();
  }
  CHECK_EQ(app.memory_stats().widgets, baseline);
}

TEST(memory_window_churn_returns_to_baseline) {
  App app(headless());
  auto keep = app.open_window();  // so closing the others does not end the app
  const size_t baseline = app.memory_stats().widgets;
  for (int round = 0; round < 300; ++round) {
    auto win = app.open_window({"t", {200, 150}});
    build_controls(win->add<Column>());
    testing::Driver d(win);
    win->close();
    testing::Driver(keep).frame();  // the close is processed at the next safe point
  }
  CHECK_EQ(app.memory_stats().widgets, baseline);
}

TEST(memory_repaint_allocates_nothing_once_warm) {
  App app(headless());
  auto win = app.open_window({"t", {400, 600}});
  auto col = win->add<Column>(StackOptions{8, Insets::all(16), Align::stretch, Align::start});
  build_controls(col);
  auto hover_target = col->add<Button>("Hover me");
  testing::Driver d(win);
  // Warm up: every glyph, shadow corner and scratch buffer the screen needs.
  for (int i = 0; i < 3; ++i) {
    d.move(hover_target->bounds().center());
    d.advance(300ms);
    d.move({1, 1});
    d.advance(300ms);
  }
  alloc_count::Scope scope;
  d.move(hover_target->bounds().center());
  d.advance(300ms);  // a whole hover transition: many animated repaints
  d.move({1, 1});
  d.advance(300ms);
  CHECK_EQ(scope.allocations(), 0LL);
}

TEST(memory_stats_report_the_frame_buffer) {
  App app(headless());
  auto win = app.open_window({"t", {300, 200}});
  testing::Driver d(win);
  const MemoryStats m = app.memory_stats();
  CHECK_EQ(m.frame_buffer_bytes, size_t{300 * 200 * 4});
  CHECK(m.glyph_cache_bytes < (4u << 20) + (1u << 20));
}
