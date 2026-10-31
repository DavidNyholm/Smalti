// Typed text: strict UTF-8 decoding, the characters a text event never carries, and UTF-16
// surrogate joining.
#include "platform/text_input.hpp"

#include <cstdint>

namespace smalti::detail {

namespace {

// The length of the UTF-8 sequence at s[i] and the code point it encodes; length 0 if it is not
// valid UTF-8 (cut short, overlong, a surrogate, beyond U+10FFFF).
size_t decode(std::string_view s, size_t i, uint32_t& cp) {
  const auto byte = [&](size_t k) { return static_cast<uint8_t>(s[k]); };
  const uint8_t lead = byte(i);
  if (lead < 0x80) {
    cp = lead;
    return 1;
  }
  size_t length = 0;
  uint32_t smallest = 0;
  if ((lead & 0xE0) == 0xC0) {
    length = 2, cp = lead & 0x1Fu, smallest = 0x80;
  } else if ((lead & 0xF0) == 0xE0) {
    length = 3, cp = lead & 0x0Fu, smallest = 0x800;
  } else if ((lead & 0xF8) == 0xF0) {
    length = 4, cp = lead & 0x07u, smallest = 0x10000;
  } else {
    return 0;
  }
  if (s.size() - i < length) return 0;
  for (size_t k = 1; k < length; ++k) {
    if ((byte(i + k) & 0xC0) != 0x80) return 0;
    cp = cp << 6 | (byte(i + k) & 0x3Fu);
  }
  if (cp < smallest || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
  return length;
}

// C0 controls, DEL and the C1 controls: what keys, not text, carry.
bool is_control(uint32_t cp) { return cp < 0x20 || (cp >= 0x7F && cp <= 0x9F); }

void append_utf8(std::string& out, uint32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | cp >> 6);
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | cp >> 12);
    out += static_cast<char>(0x80 | (cp >> 6 & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | cp >> 18);
    out += static_cast<char>(0x80 | (cp >> 12 & 0x3F));
    out += static_cast<char>(0x80 | (cp >> 6 & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

}  // namespace

std::string utf8_of(uint32_t code_point) {
  std::string out;
  if (code_point <= 0x10FFFF && !(code_point >= 0xD800 && code_point <= 0xDFFF)) {
    append_utf8(out, code_point);
  }
  return out;
}

std::string typed_text(std::string_view utf8) {
  std::string out;
  out.reserve(utf8.size());
  for (size_t i = 0; i < utf8.size();) {
    uint32_t cp = 0;
    const size_t n = decode(utf8, i, cp);
    if (n == 0) {
      ++i;  // an invalid byte: drop it and look again from the next one
      continue;
    }
    if (!is_control(cp)) out.append(utf8.substr(i, n));
    i += n;
  }
  return out;
}

std::string Utf16Joiner::push(char16_t unit) {
  std::string out;
  if (unit >= 0xD800 && unit <= 0xDBFF) {
    high_ = unit;  // a high surrogate replaces any lone one before it
    return out;
  }
  if (unit >= 0xDC00 && unit <= 0xDFFF) {
    if (high_ != 0) {
      append_utf8(out, 0x10000 + ((static_cast<uint32_t>(high_) - 0xD800) << 10) +
                           (static_cast<uint32_t>(unit) - 0xDC00));
    }
    high_ = 0;
    return out;
  }
  high_ = 0;
  append_utf8(out, unit);
  return out;
}

}  // namespace smalti::detail
