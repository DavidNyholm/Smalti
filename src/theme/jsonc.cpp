// The jsonc reader: recursive descent over text that is checked to be UTF-8 first, keeping the
// line and column of every value so later checks can point at it.
#include "theme/jsonc.hpp"

#include <charconv>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>

namespace smalti::detail {

namespace {

constexpr std::string_view kByteOrderMark = "\xEF\xBB\xBF";
// How much of an unexpected word an error message quotes.
constexpr size_t kMaxQuotedWord = 32;
constexpr const char* kSingleQuotes =
    "strings and keys are written in double quotes, not single quotes";
// A function, not a std::string constant: a theme may be read during static initialisation,
// before such a constant in this file would be constructed.
std::string too_deep() {
  return "objects and arrays are nested more than " + std::to_string(kMaxJsonDepth) + " deep";
}

// The length of the UTF-8 sequence that starts at s[i], or 0 if it is not valid UTF-8: cut
// short, overlong, a surrogate, or beyond U+10FFFF.
size_t utf8_length(std::string_view s, size_t i) {
  const auto byte = [&](size_t k) { return static_cast<uint8_t>(s[k]); };
  const uint8_t lead = byte(i);
  if (lead < 0x80) return 1;
  size_t length = 0;
  uint32_t cp = 0, smallest = 0;
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

bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool is_word_char(char c) {
  return is_letter(c) || is_digit(c) || c == '_' || c == '.' || c == '-';
}

int hex_value(char c) {
  if (is_digit(c)) return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

class Reader {
 public:
  explicit Reader(std::string_view text) : s_(text) {}

  JsonParse run() {
    JsonParse out;
    if (s_.size() > kMaxJsonBytes) {
      fail("the file is larger than " + std::to_string(kMaxJsonBytes >> 20) + " MB");
    } else {
      if (s_.starts_with(kByteOrderMark)) i_ = kByteOrderMark.size();
      if (check_utf8() && skip_space()) {
        if (at_end()) {
          fail("the file holds no value; a theme is an object: { ... }");
        } else if (read_value(out.root, 0) && skip_space() && !at_end()) {
          fail(out.root.kind == JsonKind::object ? "unexpected text after the closing '}'"
                                                 : "unexpected text after the value");
        }
      }
    }
    out.error = std::move(error_);
    return out;
  }

 private:
  bool at_end() const { return i_ >= s_.size(); }
  char peek() const { return at_end() ? '\0' : s_[i_]; }
  char peek_at(size_t ahead) const { return i_ + ahead < s_.size() ? s_[i_ + ahead] : '\0'; }

  // Consumes one character (code point). The text is valid UTF-8 by the time this runs.
  void step() {
    if (s_[i_] == '\n') {
      ++pos_.line;
      pos_.column = 1;
      ++i_;
    } else {
      i_ += utf8_length(s_, i_);
      ++pos_.column;
    }
  }

  bool fail(std::string message) { return fail_at(pos_, std::move(message)); }
  bool fail_at(TextPos pos, std::string message) {
    if (!error_) error_ = JsonError{pos, std::move(message)};
    return false;
  }

  // Walks the whole text once, so every later step can assume valid UTF-8.
  bool check_utf8() {
    const size_t start = i_;
    const TextPos start_pos = pos_;
    while (!at_end()) {
      if (utf8_length(s_, i_) == 0) return fail("the file is not valid UTF-8 here");
      step();
    }
    i_ = start;
    pos_ = start_pos;
    return true;
  }

  // What the reader found where it expected something else, for error messages.
  std::string found() const {
    if (at_end()) return "the end of the file";
    const char c = s_[i_];
    if (is_letter(c)) {
      size_t n = 0;
      while (i_ + n < s_.size() && is_word_char(s_[i_ + n]) && n < kMaxQuotedWord) ++n;
      return "\"" + std::string(s_.substr(i_, n)) + "\"";
    }
    if (c > ' ' && c < 0x7F) return std::string("'") + c + "'";
    if (static_cast<uint8_t>(c) < 0x80) return "a control character";
    return "a non-ASCII character";
  }

  // Whitespace and both kinds of comment.
  bool skip_space() {
    for (;;) {
      const char c = peek();
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
        step();
      } else if (c == '/' && peek_at(1) == '/') {
        while (!at_end() && peek() != '\n') step();
      } else if (c == '/' && peek_at(1) == '*') {
        const TextPos start = pos_;
        step();
        step();
        while (!(peek() == '*' && peek_at(1) == '/')) {
          if (at_end()) return fail_at(start, "this comment never ends; its */ is missing");
          step();
        }
        step();
        step();
      } else {
        return true;
      }
    }
  }

  bool read_value(JsonValue& out, int depth) {
    out.pos = pos_;
    if (++values_ > kMaxJsonValues)
      return fail("the file holds more than " + std::to_string(kMaxJsonValues) + " values");
    const char c = peek();
    if (c == '{') return read_object(out, depth + 1);
    if (c == '[') return read_array(out, depth + 1);
    if (c == '"') {
      out.kind = JsonKind::string;
      return read_string(out.string);
    }
    if (c == '-' || is_digit(c)) return read_number(out);
    if (c == '\'') return fail(kSingleQuotes);
    for (const auto& [word, kind, flag] : {std::tuple{"true", JsonKind::boolean, true},
                                           std::tuple{"false", JsonKind::boolean, false},
                                           std::tuple{"null", JsonKind::null, false}}) {
      const std::string_view w = word;
      if (s_.substr(i_).starts_with(w) && !is_word_char(peek_at(w.size()))) {
        for (size_t k = 0; k < w.size(); ++k) step();
        out.kind = kind;
        out.boolean = flag;
        return true;
      }
    }
    if (is_letter(c)) {
      return fail("expected a value, found " + found() +
                  "; strings and token names are written in double quotes");
    }
    return fail("expected a value, found " + found());
  }

  bool read_object(JsonValue& out, int depth) {
    out.kind = JsonKind::object;
    if (depth > kMaxJsonDepth) return fail(too_deep());
    step();  // {
    if (!skip_space()) return false;
    if (peek() == '}') {
      step();
      return true;
    }
    for (;;) {
      if (peek() != '"') {
        if (peek() == '\'') return fail(kSingleQuotes);
        if (is_letter(peek())) {
          return fail("keys are written in double quotes: found " + found());
        }
        return fail("expected a key in double quotes, found " + found());
      }
      JsonMember& m = out.members.emplace_back();
      m.pos = pos_;
      if (!read_string(m.key) || !skip_space()) return false;
      if (peek() != ':') return fail("expected ':' after the key, found " + found());
      step();
      if (!skip_space() || !read_value(m.value, depth) || !skip_space()) return false;
      if (peek() == ',') {
        step();
        if (!skip_space()) return false;
        if (peek() == '}') {  // a trailing comma
          step();
          return true;
        }
      } else if (peek() == '}') {
        step();
        return true;
      } else if (peek() == '"') {
        return fail("expected ',' before this key");
      } else {
        return fail("expected ',' or '}' after the value, found " + found());
      }
    }
  }

  bool read_array(JsonValue& out, int depth) {
    out.kind = JsonKind::array;
    if (depth > kMaxJsonDepth) return fail(too_deep());
    step();  // [
    if (!skip_space()) return false;
    if (peek() == ']') {
      step();
      return true;
    }
    for (;;) {
      if (!read_value(out.items.emplace_back(), depth) || !skip_space()) return false;
      if (peek() == ',') {
        step();
        if (!skip_space()) return false;
        if (peek() == ']') {  // a trailing comma
          step();
          return true;
        }
      } else if (peek() == ']') {
        step();
        return true;
      } else {
        return fail("expected ',' or ']' after the value, found " + found());
      }
    }
  }

  bool read_string(std::string& out) {
    const TextPos start = pos_;
    step();  // "
    for (;;) {
      if (out.size() > kMaxJsonStringBytes)
        return fail_at(start,
                       "a string longer than " + std::to_string(kMaxJsonStringBytes >> 10) + " KB");
      if (at_end()) return fail_at(start, "this string never ends; its closing \" is missing");
      const char c = peek();
      if (c == '"') {
        step();
        return true;
      }
      if (c == '\n') {
        return fail_at(start, "this string is not closed before the end of the line");
      }
      if (static_cast<uint8_t>(c) < 0x20) {
        return fail("a control character in a string; write it as an escape such as \\t");
      }
      if (c == '\\') {
        if (!read_escape(out)) return false;
        continue;
      }
      const size_t n = utf8_length(s_, i_);
      out.append(s_.substr(i_, n));
      step();
    }
  }

  bool read_escape(std::string& out) {
    const TextPos at = pos_;
    step();  // backslash
    const char e = peek();
    const char* simple = nullptr;
    switch (e) {
      case '"':
        simple = "\"";
        break;
      case '\\':
        simple = "\\";
        break;
      case '/':
        simple = "/";
        break;
      case 'b':
        simple = "\b";
        break;
      case 'f':
        simple = "\f";
        break;
      case 'n':
        simple = "\n";
        break;
      case 'r':
        simple = "\r";
        break;
      case 't':
        simple = "\t";
        break;
      case 'u':
        break;
      default:
        return fail_at(at,
                       "unknown escape; JSON has \\\" \\\\ \\/ \\b \\f \\n \\r \\t and \\uXXXX");
    }
    step();
    if (simple) {
      out += simple;
      return true;
    }
    uint32_t unit = 0;
    if (!hex4(unit)) return fail_at(at, "\\u needs four hexadecimal digits");
    if (unit >= 0xDC00 && unit <= 0xDFFF) {
      return fail_at(at, "a \\u escape of a low surrogate without a high one before it");
    }
    if (unit >= 0xD800 && unit <= 0xDBFF) {
      uint32_t low = 0;
      if (peek() != '\\' || peek_at(1) != 'u') {
        return fail_at(at, "a \\u escape of a high surrogate without a low one after it");
      }
      step();
      step();
      if (!hex4(low) || low < 0xDC00 || low > 0xDFFF) {
        return fail_at(at, "a \\u escape of a high surrogate without a low one after it");
      }
      unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
    }
    append_utf8(out, unit);
    return true;
  }

  bool hex4(uint32_t& out) {
    for (int k = 0; k < 4; ++k) {
      const int v = hex_value(peek());
      if (v < 0) return false;
      out = out << 4 | static_cast<uint32_t>(v);
      step();
    }
    return true;
  }

  bool read_number(JsonValue& out) {
    out.kind = JsonKind::number;
    const size_t start = i_;
    if (peek() == '-') step();
    if (peek() == '0') {
      step();
      if (is_digit(peek()))
        return fail_at(out.pos, "a number does not start with 0: write 7, not 07");
    } else if (is_digit(peek())) {
      while (is_digit(peek())) step();
    } else {
      return fail("expected a digit after '-'");
    }
    if (peek() == '.') {
      step();
      if (!is_digit(peek())) return fail("expected a digit after the decimal point");
      while (is_digit(peek())) step();
    }
    if (peek() == 'e' || peek() == 'E') {
      step();
      if (peek() == '+' || peek() == '-') step();
      if (!is_digit(peek())) return fail("expected a digit in the exponent");
      while (is_digit(peek())) step();
    }
    if (is_letter(peek()) || peek() == '%') {
      return fail("unexpected " + found() +
                  " after the number; a size is a plain number, and a duration a string such "
                  "as \"120ms\"");
    }
    const std::string_view text = s_.substr(start, i_ - start);
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), out.number);
    if (ec != std::errc{} || end != text.data() + text.size()) {
      return fail_at(out.pos, "this number is out of range");
    }
    return true;
  }

  std::string_view s_;
  size_t i_ = 0;
  TextPos pos_;
  size_t values_ = 0;
  std::optional<JsonError> error_;
};

}  // namespace

JsonParse parse_jsonc(std::string_view text) { return Reader(text).run(); }

const char* json_kind_name(JsonKind kind) {
  switch (kind) {
    case JsonKind::null:
      return "null";
    case JsonKind::boolean:
      return "true or false";
    case JsonKind::number:
      return "a number";
    case JsonKind::string:
      return "a string";
    case JsonKind::array:
      return "an array";
    case JsonKind::object:
      return "an object";
  }
  return "a value";
}

}  // namespace smalti::detail
