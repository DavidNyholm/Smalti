// Typed text on its way from a backend to a widget: cleaned to what a text widget can take, and,
// for Windows, put back together from the UTF-16 code units it arrives in.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace smalti::detail {

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
