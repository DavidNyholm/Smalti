// The built-in widgets driven by simulated mouse and keyboard, and what they draw.
#include "check.hpp"
#include "support.hpp"

using namespace smalti;
using namespace std::chrono_literals;

namespace {

uint8_t red(uint32_t px) { return static_cast<uint8_t>(px >> 16); }

}  // namespace

TEST(button_clicks_only_when_released_inside) {
  App app(headless());
  auto win = app.open_window({"t", {300, 200}});
  auto col = win->add<Column>(StackOptions{0, Insets::all(20), Align::start, Align::start});
  auto button = col->add<Button>("Save", ButtonVariant::primary);
  int clicks = 0;
  button->on_click([&] { ++clicks; });
  testing::Driver d(win);
  d.click(button);
  CHECK_EQ(clicks, 1);
  const Point inside = button->bounds().center();
  d.press(inside);
  d.release({280, 180});  // dragged off before letting go: no click
  CHECK_EQ(clicks, 1);
  button->set_enabled(false);
  d.click(button);
  CHECK_EQ(clicks, 1);  // disabled
}

TEST(button_works_from_the_keyboard_and_tab_moves_focus) {
  App app(headless());
  auto win = app.open_window({"t", {300, 200}});
  auto col = win->add<Column>(StackOptions{8, Insets::all(20), Align::start, Align::start});
  auto first = col->add<Button>("First");
  col->add<Label>("not focusable");
  auto second = col->add<Button>("Second");
  int a = 0, b = 0;
  first->on_click([&] { ++a; });
  second->on_click([&] { ++b; });
  testing::Driver d(win);
  d.key(Key::tab);
  CHECK(first->has_focus());
  d.key(Key::space);
  CHECK_EQ(a, 1);
  d.key(Key::tab);
  CHECK(second->has_focus());
  d.key(Key::enter);
  CHECK_EQ(b, 1);
  d.key(Key::tab, {true});  // shift+tab goes back
  CHECK(first->has_focus());
  d.key(Key::tab);
  d.key(Key::tab);  // wraps around
  CHECK(first->has_focus());
}

TEST(checkbox_switch_and_radio_group) {
  App app(headless());
  auto win = app.open_window({"t", {300, 300}});
  auto col = win->add<Column>(StackOptions{8, Insets::all(10), Align::start, Align::start});
  auto check = col->add<CheckBox>("Dithering");
  auto sw = col->add<Switch>("Live reload", true);
  auto group = col->add<Column>();
  auto daily = group->add<RadioButton>("Daily", true);
  auto weekly = group->add<RadioButton>("Weekly");
  auto monthly = group->add<RadioButton>("Monthly");
  bool last_check = false;
  check->on_change([&](bool v) { last_check = v; });
  testing::Driver d(win);
  d.click(check);
  CHECK(check->checked());
  CHECK(last_check);
  d.click(sw);
  CHECK(!sw->is_on());
  d.click(monthly);
  CHECK(monthly->selected());
  CHECK(!daily->selected());
  // Arrow keys move the selection within the group, wrapping around.
  d.key(Key::down);
  CHECK(daily->selected());
  CHECK(daily->has_focus());
  d.key(Key::down);
  CHECK(weekly->selected());
  CHECK(!monthly->selected());
}

TEST(slider_follows_drag_keys_and_step) {
  App app(headless());
  auto win = app.open_window({"t", {300, 100}});
  auto col = win->add<Column>(StackOptions{0, Insets::all(20), Align::stretch, Align::start});
  auto slider = col->add<Slider>(SliderOptions{0, 100, 50, 10});
  float reported = -1;
  slider->on_change([&](float v) { reported = v; });
  testing::Driver d(win);
  const Rect b = slider->bounds();
  d.press({b.x + 8, b.center().y});  // the left end of the track
  CHECK_EQ(slider->value(), 0.0f);
  d.move({b.right() - 8, b.center().y});  // dragged to the right end
  CHECK_EQ(slider->value(), 100.0f);
  d.release({b.right() - 8, b.center().y});
  CHECK_EQ(reported, 100.0f);
  d.key(Key::left);
  CHECK_EQ(slider->value(), 90.0f);
  d.key(Key::home);
  CHECK_EQ(slider->value(), 0.0f);
  slider->set_value(44);
  CHECK_EQ(slider->value(), 40.0f);  // snapped to the step
}

TEST(motion_settles_and_then_requests_no_frames) {
  // Hover animates the button, frames run at the display rate while it does, and once it has
  // settled nothing more is drawn: an idle app costs nothing.
  App app(headless());
  auto win = app.open_window({"t", {300, 200}});
  auto button = win->add<Column>(StackOptions{0, Insets::all(20), Align::start, Align::start})
                    ->add<Button>("Hover");
  testing::Driver d(win);
  d.advance(1s);
  CHECK(!d.frame_requested());
  const int before = d.frames_drawn();
  d.move(button->bounds().center());
  CHECK(d.frame_requested());
  d.advance(1s);
  const int during = d.frames_drawn() - before;
  CHECK(during >= 3);  // the 120 ms transition drew several frames
  CHECK(!d.frame_requested());
  const int settled = d.frames_drawn();
  d.advance(5s);
  CHECK_EQ(d.frames_drawn(), settled);
}

TEST(indeterminate_progress_keeps_animating_until_hidden) {
  App app(headless());
  auto win = app.open_window({"t", {300, 100}});
  auto bar = win->add<Column>(StackOptions{0, Insets::all(20), Align::stretch, Align::start})
                 ->add<ProgressBar>();
  bar->set_indeterminate(true);
  testing::Driver d(win);
  const int before = d.frames_drawn();
  d.advance(500ms);
  CHECK(d.frames_drawn() - before >= 25);  // about 30 frames at 60 Hz
  bar->set_visible(false);
  d.advance(100ms);
  const int after_hide = d.frames_drawn();
  d.advance(1s);
  CHECK_EQ(d.frames_drawn(), after_hide);
}

TEST(paint_background_and_primary_button_colours) {
  App app(headless(&theme_light()));
  auto win = app.open_window({"t", {200, 100}});
  auto button = win->add<Column>(StackOptions{0, Insets::all(20), Align::start, Align::start})
                    ->add<Button>("OK", ButtonVariant::primary);
  testing::Driver d(win);
  const uint32_t bg = d.pixel(5, 5);
  CHECK_EQ(red(bg), static_cast<uint8_t>(0xFA));  // Light's background
  // A point inside the button but left of the text: the accent fill.
  const Rect b = button->bounds();
  const uint32_t fill = d.pixel(static_cast<int>(b.x + 3), static_cast<int>(b.center().y));
  CHECK_EQ(fill & 0xFFFFFF, 0x2F6DEBu);
}

TEST(paint_follows_the_window_scale) {
  App app(headless());
  auto win = app.open_window({"t", {200, 100}});
  testing::Driver d(win);
  CHECK_EQ(d.width(), 200);
  d.resize({200, 100}, 1.5f);
  d.frame();
  CHECK_EQ(d.width(), 300);
  CHECK_EQ(d.height(), 150);
  CHECK_EQ(win->client_size().width, 200.0f);
}

TEST(window_close_ends_run_and_destroys_widgets) {
  App app(headless());
  auto win = app.open_window();
  auto button = win->add<Button>("Quit");
  button->on_click([win] { win->close(); });
  testing::Driver d(win);
  d.click(button);
  CHECK(!win);
  CHECK(!button);
  CHECK_EQ(app.run(), 0);  // nothing left to run
}
