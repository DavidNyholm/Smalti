// Clipboard text: made plain both ways, Windows line endings and Latin-1 for the backends, the
// size limit on reading, and the round trip through the App.
#include <string>

#include "check.hpp"
#include "platform/text_input.hpp"
#include "support.hpp"

using namespace smalti;

namespace {
const std::string kReplacement = "\xEF\xBF\xBD";  // U+FFFD in UTF-8
}  // namespace

TEST(clipboard_plain_text_has_newlines_valid_utf8_and_no_nul) {
  using detail::plain_text;
  CHECK_EQ(plain_text("abc"), std::string("abc"));
  CHECK_EQ(plain_text("a\r\nb\rc\nd"), std::string("a\nb\nc\nd"));
  CHECK_EQ(plain_text("\r\r\n\n"), std::string("\n\n\n"));
  CHECK_EQ(plain_text("end\r"), std::string("end\n"));
  CHECK_EQ(plain_text("a\tb"), std::string("a\tb"));  // other controls are text here
  CHECK_EQ(plain_text(std::string("a\0b", 3)), std::string("ab"));
  const std::string letters = "\xC3\xA9\xE6\x97\xA5\xF0\x9F\x98\x80";  // e-acute, a CJK, an emoji
  CHECK_EQ(plain_text(letters), letters);
  // Each invalid byte becomes U+FFFD: a stray byte, a sequence cut short, an overlong one.
  CHECK_EQ(plain_text("a\xFF"
                      "b"),
           "a" + kReplacement + "b");
  CHECK_EQ(plain_text("a\xE2\x82"), "a" + kReplacement + kReplacement);
  CHECK_EQ(plain_text("\xC0\xAF"), kReplacement + kReplacement);
}

TEST(clipboard_windows_line_endings) {
  CHECK_EQ(detail::with_crlf("a\nb\n"), std::string("a\r\nb\r\n"));
  CHECK_EQ(detail::with_crlf("\n\n"), std::string("\r\n\r\n"));
  CHECK_EQ(detail::with_crlf(""), std::string(""));
  CHECK_EQ(detail::with_crlf("no break"), std::string("no break"));
}

TEST(clipboard_latin1_both_ways) {
  CHECK_EQ(detail::latin1_of("abc\xC3\xA9"), std::string("abc\xE9"));
  CHECK_EQ(detail::latin1_of("\xE2\x82\xAC"), std::string("?"));  // the euro sign: beyond U+00FF
  CHECK_EQ(detail::latin1_of("\xF0\x9F\x98\x80!"), std::string("?!"));  // one character, one '?'
  CHECK_EQ(detail::latin1_of("\xFF"), std::string("?"));
  CHECK_EQ(detail::utf8_of_latin1("a\xE9\xFF"), std::string("a\xC3\xA9\xC3\xBF"));
  std::string all;
  for (int c = 0; c < 256; ++c) all += static_cast<char>(c);
  CHECK_EQ(detail::latin1_of(detail::utf8_of_latin1(all)), all);
}

TEST(clipboard_round_trip_through_the_app) {
  App app(headless());
  CHECK(!app.clipboard_text().has_value());  // nothing was ever copied
  CHECK(app.set_clipboard_text("line 1\r\nline 2"));
  CHECK_EQ(app.clipboard_text().value_or("?"), std::string("line 1\nline 2"));
  CHECK(app.set_clipboard_text(""));  // empty text is still text
  CHECK_EQ(app.clipboard_text().value_or("?"), std::string(""));
}

TEST(clipboard_text_the_app_copies_is_plain_for_other_programs) {
  App app(headless());
  auto win = app.open_window({"t", {100, 100}});
  testing::Driver d(win);
  CHECK(app.set_clipboard_text(std::string("a\r\nb\rc\0d\xFF", 9)));
  CHECK_EQ(d.clipboard_contents().value_or("?"), "a\nb\ncd" + kReplacement);
}

TEST(clipboard_text_from_another_program_is_made_plain_and_limited) {
  App app(headless());
  auto win = app.open_window({"t", {100, 100}});
  testing::Driver d(win);
  d.put_on_clipboard(std::string("a\r\nb\0c\xFF", 7));
  CHECK_EQ(app.clipboard_text().value_or("?"), "a\nbc" + kReplacement);
  d.put_on_clipboard(std::string(detail::kMaxClipboardBytes, 'x'));
  CHECK_EQ(app.clipboard_text().value_or("").size(), detail::kMaxClipboardBytes);
  d.put_on_clipboard(std::string(detail::kMaxClipboardBytes + 1, 'x'));
  CHECK(!app.clipboard_text().has_value());
}
