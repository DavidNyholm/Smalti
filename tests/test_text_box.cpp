// TextBox through the headless backend: typing, keys, the mouse, the clipboard, undo, a caret
// that blinks on timed redraws and then stops, scrolling, password mode and the length limit.
#include <cmath>
#include <string>
#include <vector>

#include "check.hpp"
#include "support.hpp"

using namespace smalti;
using namespace std::chrono_literals;

namespace {

const std::string kEAcute = "\xC3\xA9";

Modifiers with(bool ctrl, bool shift) {
  Modifiers m;
  m.ctrl = ctrl;
  m.shift = shift;
  return m;
}
const Modifiers kCtrl = with(true, false), kShift = with(false, true),
                kCtrlShift = with(true, true);

TextBoxOptions options(std::string text, bool password = false, size_t max_length = 0) {
  TextBoxOptions o;
  o.text = std::move(text);
  o.password = password;
  o.max_length = max_length;
  return o;
}

struct Fixture {
  App app{headless()};
  Ref<Window> win = app.open_window({"t", {320, 80}});
  Ref<Column> col = win->add<Column>(StackOptions{8, Insets::all(10), Align::start, Align::start});
  Ref<TextBox> box;
  testing::Driver d{win};
  explicit Fixture(TextBoxOptions options = {}) {
    box = col->add<TextBox>(std::move(options));
    d.frame();
  }
  // Selects nothing new: copies what is selected and returns it.
  std::string selected() {
    d.put_on_clipboard("");
    d.key(Key::c, kCtrl);
    return d.clipboard_contents().value_or("?");
  }
  float right_of_text() const { return box->bounds().right() - 4; }
  float left_of_text() const { return box->bounds().x + 11; }
};

}  // namespace

TEST(text_box_typing_reaches_the_text_and_on_change) {
  Fixture f;
  std::vector<std::string> changes;
  f.box->on_change([&](const std::string& t) { changes.push_back(t); });
  CHECK(!f.d.text_input_active());
  f.d.click(f.box);
  CHECK(f.d.text_input_active());  // the input method is on for a text box
  f.d.type("h" + kEAcute + "j");
  f.d.type("!");
  CHECK_EQ(f.box->text(), "h" + kEAcute + "j!");
  CHECK_EQ(changes.size(), size_t{2});
  f.d.key(Key::backspace);
  CHECK_EQ(f.box->text(), "h" + kEAcute + "j");
  f.d.key(Key::backspace, kCtrl);  // a word
  CHECK_EQ(f.box->text(), std::string(""));
  CHECK_EQ(changes.size(), size_t{4});
  f.box->set_text("set\nby the program");  // made one line; no on_change
  CHECK_EQ(f.box->text(), std::string("set by the program"));
  CHECK_EQ(changes.size(), size_t{4});
  f.d.key(Key::backspace);  // nothing selected, the caret at the end
  CHECK_EQ(f.box->text(), std::string("set by the progra"));
}

TEST(text_box_keys_move_select_and_replace) {
  Fixture f;
  f.d.click(f.box);
  f.d.type("one two");
  f.d.key(Key::left, kCtrlShift);  // selects "two"
  CHECK_EQ(f.selected(), std::string("two"));
  f.d.type("2");
  CHECK_EQ(f.box->text(), std::string("one 2"));
  f.d.key(Key::home);
  f.d.key(Key::right, kShift);
  f.d.key(Key::right, kShift);
  CHECK_EQ(f.selected(), std::string("on"));
  f.d.key(Key::delete_);
  CHECK_EQ(f.box->text(), std::string("e 2"));
  f.d.key(Key::end, kShift);  // from the caret to the end
  CHECK_EQ(f.selected(), std::string("e 2"));
  f.d.key(Key::a, kCtrl);
  f.d.type("x");
  CHECK_EQ(f.box->text(), std::string("x"));
  bool submitted = false;
  f.box->on_submit([&](const std::string& t) { submitted = t == "x"; });
  f.d.key(Key::enter);
  CHECK(submitted);
}

TEST(text_box_clipboard_by_both_sets_of_shortcuts) {
  Fixture f;
  f.d.click(f.box);
  f.d.type("copy me");
  f.d.key(Key::a, kCtrl);
  f.d.key(Key::insert, kCtrl);
  CHECK_EQ(f.d.clipboard_contents().value_or("?"), std::string("copy me"));
  f.d.key(Key::x, kCtrl);
  CHECK_EQ(f.box->text(), std::string(""));
  CHECK_EQ(f.d.clipboard_contents().value_or("?"), std::string("copy me"));
  f.d.put_on_clipboard("line one\r\nline two\n");  // from another program
  f.d.key(Key::v, kCtrl);
  CHECK_EQ(f.box->text(), std::string("line one line two"));
  f.d.key(Key::a, kCtrl);
  f.d.key(Key::delete_, kShift);  // cut
  CHECK_EQ(f.box->text(), std::string(""));
  f.d.key(Key::insert, kShift);  // paste
  CHECK_EQ(f.box->text(), std::string("line one line two"));
}

TEST(text_box_undo_and_redo) {
  Fixture f;
  f.d.click(f.box);
  for (const char c : std::string("abc")) f.d.type(std::string(1, c));
  f.d.advance(1500ms);  // a pause ends the step
  f.d.type("d");
  f.d.key(Key::z, kCtrl);
  CHECK_EQ(f.box->text(), std::string("abc"));
  f.d.key(Key::z, kCtrl);
  CHECK_EQ(f.box->text(), std::string(""));
  f.d.key(Key::y, kCtrl);
  CHECK_EQ(f.box->text(), std::string("abc"));
  f.d.key(Key::z, kCtrlShift);
  CHECK_EQ(f.box->text(), std::string("abcd"));
}

TEST(text_box_mouse_places_the_caret_and_selects) {
  Fixture f(options("hello big world"));
  f.d.click({f.left_of_text(), f.box->bounds().center().y});
  f.d.type(">");
  CHECK_EQ(f.box->text(), std::string(">hello big world"));
  const Point end{f.right_of_text(), f.box->bounds().center().y};
  f.d.click(end);
  f.d.type("<");
  CHECK_EQ(f.box->text(), std::string(">hello big world<"));
  // Shift+click extends from the caret.
  const Point start{f.left_of_text(), end.y};
  f.d.press(start, MouseButton::left, kShift);
  f.d.release(start);
  CHECK_EQ(f.selected(), std::string(">hello big world<"));
  // A double click selects a word, a triple click everything.
  const Point in_hello{f.left_of_text() + 12, end.y};
  f.d.click(in_hello);
  f.d.press(in_hello, MouseButton::left, {}, 2);
  f.d.release(in_hello);
  CHECK_EQ(f.selected(), std::string("hello"));
  f.d.press(in_hello, MouseButton::left, {}, 3);
  f.d.release(in_hello);
  CHECK_EQ(f.selected(), std::string(">hello big world<"));
  // Dragging selects.
  f.d.press(start);
  f.d.move(end);
  f.d.release(end);
  CHECK_EQ(f.selected(), std::string(">hello big world<"));
}

TEST(text_box_caret_blinks_on_timed_frames_then_stops) {
  Fixture f(options("ab "));
  f.d.click(f.box);
  f.d.frame();
  const Rect caret = f.d.text_input_caret();
  const int x = static_cast<int>(std::lround(caret.x));
  const int y = static_cast<int>(caret.y + caret.height / 2);
  const uint32_t shown = f.d.pixel(x, y);
  f.d.advance(600ms);  // past 530 ms: hidden
  const uint32_t hidden = f.d.pixel(x, y);
  CHECK(shown != hidden);
  CHECK_EQ(hidden, f.d.pixel(static_cast<int>(f.right_of_text()), y));  // the box's fill
  // Two frames a second while it blinks.
  int frames = f.d.frames_drawn();
  f.d.advance(2000ms);
  const int blinking = f.d.frames_drawn() - frames;
  CHECK(blinking >= 3 && blinking <= 5);
  // After 10 s without input it stays shown, and costs nothing.
  f.d.advance(9000ms);
  CHECK_EQ(f.d.pixel(x, y), shown);
  frames = f.d.frames_drawn();
  f.d.advance(20000ms);
  CHECK_EQ(f.d.frames_drawn() - frames, 0);
  CHECK(!f.d.frame_requested());
  // A repaint for another reason, at any moment, still finds it shown.
  for (int i = 0; i < 4; ++i) {
    f.d.advance(300ms);
    f.box->invalidate();
    f.d.frame();
    CHECK_EQ(f.d.pixel(x, y), shown);
  }
  // Typing shows it again and starts the blinking over.
  f.d.type("c");
  f.d.frame();
  frames = f.d.frames_drawn();
  f.d.advance(600ms);
  CHECK_EQ(f.d.frames_drawn() - frames, 1);
}

TEST(text_box_scrolls_to_keep_the_caret_in_view) {
  Fixture f;
  f.d.click(f.box);
  f.d.type(std::string(80, 'w'));
  f.d.frame();
  const Rect b = f.box->bounds();
  Rect caret = f.d.text_input_caret();
  CHECK(caret.x > b.x && caret.x < b.right());
  CHECK(caret.x > b.center().x);  // at the right end: the start scrolled out of view
  f.d.key(Key::home);
  f.d.frame();
  caret = f.d.text_input_caret();
  CHECK(caret.x >= b.x && caret.x < b.x + 12);  // back at the left: scrolled to the start
}

TEST(text_box_password_hides_its_characters_and_keeps_them) {
  Fixture f(options("s3cret", true));
  std::string changed;
  f.box->on_change([&](const std::string& t) { changed = t; });
  f.d.click(f.box);
  f.d.key(Key::a, kCtrl);
  f.d.put_on_clipboard("unchanged");
  f.d.key(Key::c, kCtrl);
  f.d.key(Key::x, kCtrl);
  CHECK_EQ(f.d.clipboard_contents().value_or("?"), std::string("unchanged"));
  CHECK_EQ(f.box->text(), std::string("s3cret"));  // a cut that copies nothing erases nothing
  f.d.key(Key::end);
  f.d.key(Key::left);
  f.d.key(Key::backspace);  // by characters, as shown
  CHECK_EQ(f.box->text(), std::string("s3crt"));
  CHECK_EQ(changed, std::string("s3crt"));
}

TEST(text_box_length_limit_and_disabled) {
  Fixture f(options("abcdef", false, 4));
  CHECK_EQ(f.box->text(), std::string("abcd"));
  f.d.click(f.box);
  f.d.type("x");
  CHECK_EQ(f.box->text(), std::string("abcd"));
  f.box->set_enabled(false);
  f.d.frame();
  CHECK(!f.d.text_input_active());
  f.d.type("y");
  CHECK_EQ(f.box->text(), std::string("abcd"));
}
