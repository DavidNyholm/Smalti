// Text on its way between a backend and the program. Typed text: cleaned to what a text widget
// can take, and, for Windows, put back together from the UTF-16 code units it arrives in.
// Clipboard text: plain UTF-8 in the program, and what each system expects outside it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace smalti::detail {

// Clipboard text read from other programs larger than this is not read: a paste on X11 is a
// transfer from another program that must end, and the same limit holds on every platform.
inline constexpr size_t kMaxClipboardBytes = size_t{16} << 20;

// Clipboard text as the program sees it, both ways: valid UTF-8 (each invalid byte becomes
// U+FFFD), "\n" line endings (from "\r\n" and a lone "\r" too), and no NUL characters.
std::string plain_text(std::string_view utf8);

// Plain text with Windows line endings, "\r\n", as the Windows clipboard holds text.
std::string with_crlf(std::string_view plain);

// X11's STRING type is Latin-1. Characters beyond U+00FF become '?' on the way out.
std::string latin1_of(std::string_view utf8);
std::string utf8_of_latin1(std::string_view latin1);

// The typed text a widget receives: valid UTF-8 with no control characters (C0, DEL, C1).
// Keys carry Tab, Enter, Backspace and Ctrl combinations; the text event never does.
// Anything invalid is dropped, character by character.
std::string typed_text(std::string_view utf8);

// One code point in UTF-8; empty for a surrogate or anything beyond U+10FFFF.
std::string utf8_of(uint32_t code_point);

// Joins UTF-16 code units, as Windows delivers typed characters (WM_CHAR), into UTF-8: a high
// surrogate waits for the low one after it. A surrogate without its partner is dropped.
class Utf16Joiner {
 public:
  // The UTF-8 for this unit and any it completes; empty while it waits, or for a lone half.
  std::string push(char16_t unit);

 private:
  char16_t high_ = 0;  // a high surrogate waiting for its low one
};

}  // namespace smalti::detail
