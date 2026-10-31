// Typed text: cleaning it, joining Windows' UTF-16 halves, delivering it to the focused widget,
// and switching the input method on only while a widget that takes text has focus.
#include <string>

#include "check.hpp"
#include "platform/text_input.hpp"
#include "support.hpp"

using namespace smalti;

namespace {

// Takes text while focused, like a text box, with its caret where caret_x says.
class TextTarget : public Widget {
 public:
  std::string typed;
  int calls = 0;
  float caret_x = 5;
  Size measure(Size) override { return {100, 30}; }
  bool focusable() const override { return true; }
  bool accepts_text() const override { return true; }
  bool on_text(std::string_view utf8) override {
    typed += utf8;
    ++calls;
    return true;
  }
  Rect text_caret() const override {
    const Rect b = bounds();
    return {b.x + caret_x, b.y + 4, 1, b.height - 8};
  }
  void move_caret(float x) {
    caret_x = x;
    text_caret_moved();
  }
};

bool same(Rect a, Rect b) {
  return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

}  // namespace

TEST(text_input_typed_text_keeps_characters_and_drops_controls) {
  using detail::typed_text;
  CHECK_EQ(typed_text("abc"), std::string("abc"));
  CHECK_EQ(typed_text("\u00e9\u65e5\u672c"), std::string("\u00e9\u65e5\u672c"));
  CHECK_EQ(typed_text("\xF0\x9F\x98\x80"), std::string("\xF0\x9F\x98\x80"));  // outside the BMP
  // Tab, Enter, Backspace, Ctrl+A, Escape, DEL and C1 controls are keys, not text.
  CHECK_EQ(typed_text("a\tb\r\x08\x01\x1B\x7F"
                      "c"),
           std::string("abc"));
  CHECK_EQ(typed_text("x\xC2\x85y"), std::string("xy"));  // U+0085, a C1 control
  // Invalid UTF-8 is dropped byte by byte: cut short, overlong, a surrogate, a stray byte.
  CHECK_EQ(typed_text("a\xE2\x82"), std::string("a"));
  CHECK_EQ(typed_text("a\xC0\xAF"
                      "b"),
           std::string("ab"));
  CHECK_EQ(typed_text("a\xED\xA0\x80"
                      "b"),
           std::string("ab"));
  CHECK_EQ(typed_text("\xFF"
                      "z"),
           std::string("z"));
}

TEST(text_input_utf16_halves_join_and_lone_halves_drop) {
  detail::Utf16Joiner j;
  CHECK_EQ(j.push(u'a'), std::string("a"));
  CHECK_EQ(j.push(0xE9), std::string("\u00e9"));
  CHECK(j.push(0xD83D).empty());  // waits for its low half
  CHECK_EQ(j.push(0xDE00), std::string("\xF0\x9F\x98\x80"));
  CHECK(j.push(0xDE00).empty());  // a low half alone
  CHECK(j.push(0xD83D).empty());
  CHECK_EQ(j.push(u'b'), std::string("b"));  // the waiting high half is dropped
  CHECK(j.push(0xD83D).empty());
  CHECK(j.push(0xD83D).empty());  // a second high half replaces the first
  CHECK_EQ(j.push(0xDE00), std::string("\xF0\x9F\x98\x80"));
  CHECK_EQ(detail::utf8_of(0x41), std::string("A"));
  CHECK(detail::utf8_of(0xD800).empty());
  CHECK(detail::utf8_of(0x110000).empty());
}

TEST(text_input_reaches_the_focused_widget_only) {
  App app(headless());
  auto win = app.open_window({"t", {300, 200}});
  auto col = win->add<Column>(StackOptions{8, Insets::all(10), Align::start, Align::start});
  auto first = col->add<TextTarget>();
  auto second = col->add<TextTarget>();
  testing::Driver d(win);
  d.type("lost");  // nothing has focus
  first->focus();
  d.type("h\u00e9j");
  d.type("\t\x01");  // only controls: nothing to deliver
  second->focus();
  d.type("!");
  CHECK_EQ(first->typed, std::string("h\u00e9j"));
  CHECK_EQ(first->calls, 1);
  CHECK_EQ(second->typed, std::string("!"));
  // Disabling the focused widget takes the focus away with it: nothing is delivered to it.
  second->set_enabled(false);
  d.type("x");
  CHECK_EQ(second->typed, std::string("!"));
}

TEST(text_input_is_on_only_while_a_text_widget_has_focus) {
  App app(headless());
  auto win = app.open_window({"t", {300, 200}});
  auto col = win->add<Column>(StackOptions{8, Insets::all(10), Align::start, Align::start});
  auto target = col->add<TextTarget>();
  auto button = col->add<Button>("OK");
  testing::Driver d(win);
  CHECK(!d.text_input_active());
  target->focus();
  CHECK(d.text_input_active());
  CHECK(same(d.text_input_caret(), target->text_caret()));
  // The caret moves, and the input method's windows follow it.
  target->move_caret(40);
  CHECK(same(d.text_input_caret(), target->text_caret()));
  // A button never gets an input method.
  button->focus();
  CHECK(!d.text_input_active());
  target->focus();
  CHECK(d.text_input_active());
  target->set_visible(false);
  d.frame();
  CHECK(!d.text_input_active());
}

TEST(text_input_caret_follows_layout_and_scale) {
  App app(headless());
  auto win = app.open_window({"t", {300, 200}});
  auto col = win->add<Column>(StackOptions{8, Insets::all(10), Align::start, Align::start});
  auto spacer = col->add<Spacer>(0.0f, 10.0f);
  auto target = col->add<TextTarget>();
  testing::Driver d(win);
  target->focus();
  const Rect before = d.text_input_caret();
  // A layout that moves the widget moves the caret, without the widget saying so.
  spacer->set_min_size({0, 50});
  d.frame();
  CHECK(same(d.text_input_caret(), target->text_caret()));
  CHECK(d.text_input_caret().y > before.y);
  // At 200%, the caret is the same in logical pixels (the platform gets device pixels).
  d.resize({300, 200}, 2.0f);
  d.frame();
  CHECK(same(d.text_input_caret(), target->text_caret()));
}
