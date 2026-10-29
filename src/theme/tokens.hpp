// The vocabulary of theme files: every token, the kind of value it takes, and how such a
// value is written. theme.cpp reads whole files with it.
#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "smalti/theme.hpp"
#include "theme/jsonc.hpp"

namespace smalti::detail {

// A token's kind decides the values it takes and the tokens it may refer to.
enum class TokenKind : uint8_t {
  color,
  radius,
  control_size,
  font_size,
  duration,
  flag,
  blend,
  contrast
};

using ThemeField =
    std::variant<Color Theme::*, float Theme::*, bool Theme::*, std::chrono::milliseconds Theme::*,
                 TextBlend TextTuning::*, float TextTuning::*, bool TextTuning::*>;

struct TokenSpec {
  std::string_view name;
  TokenKind kind;
  ThemeField field;
};

// Every token a theme file can set, named for files, and the Theme field it sets.
inline constexpr size_t kTokenCount = 34;
extern const TokenSpec kTokens[kTokenCount];

struct TokenValue {
  Color color;
  double number = 0;  // sizes and contrast; durations in milliseconds
  bool flag = false;
  TextBlend blend = TextBlend::adaptive;
};

// A token's value as written: a literal, or another token's value (a colour's opacity scaled).
struct TokenExpr {
  TextPos pos;
  int ref = -1;          // the token it refers to; -1 for a literal
  float opacity = 1;
  TokenValue literal;
};

// A file of nothing but mistakes reports this many, then how many more there were.
inline constexpr size_t kMaxThemeErrors = 100;

// Collects a file's errors; take() gives them in file order.
class ThemeErrors {
 public:
  void add(TextPos pos, std::string message) {
    if (list_.size() < kMaxThemeErrors) {
      list_.push_back({pos.line, pos.column, std::move(message)});
    } else {
      ++dropped_;
    }
  }
  bool any() const { return !list_.empty(); }
  std::vector<ThemeError> take() {
    std::stable_sort(list_.begin(), list_.end(), [](const ThemeError& a, const ThemeError& b) {
      return a.line != b.line ? a.line < b.line : a.column < b.column;
    });
    if (dropped_ > 0) list_.push_back({0, 0, std::to_string(dropped_) + " more errors not shown"});
    return std::move(list_);
  }

 private:
  std::vector<ThemeError> list_;
  size_t dropped_ = 0;
};

template <size_t N>
inline std::optional<size_t> find_name(const std::array<std::string_view, N>& names,
                                       std::string_view s) {
  for (size_t i = 0; i < N; ++i) {
    if (names[i] == s) return i;
  }
  return std::nullopt;
}

std::optional<size_t> find_token(std::string_view name);
std::vector<std::string_view> token_names(std::optional<TokenKind> only = std::nullopt);
// "18%" gives 0.18; anything but a percentage from 0% to 100% gives nothing.
std::optional<float> parse_percent(std::string_view s);
// The message for a key that an object sets twice.
std::string set_twice(std::string_view key, TextPos first);
// Text from the file, quoted for a message: cut short, and without control characters.
std::string quote_text(std::string_view s);
// "; did you mean "accent"?" when one candidate is close to what was typed, else nothing.
std::string did_you_mean(std::string_view typed, std::span<const std::string_view> candidates);
// One token's value as the file writes it; nullopt, with the reason in `errors`, when it is
// not a value the token takes.
std::optional<TokenExpr> read_token_value(const TokenSpec& spec, const JsonValue& v,
                                          ThemeErrors& errors);

}  // namespace smalti::detail
