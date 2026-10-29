// Theme tokens: the table of every token, and reading one token's value in each form a file
// may write it (docs/Themes.md lists them).
#include "theme/tokens.hpp"

#include <array>
#include <charconv>
#include <system_error>

namespace smalti::detail {

namespace {

// How much of a key or value from the file an error message quotes.
constexpr size_t kMaxQuoted = 48;

// The ranges a file may set. They keep layout and drawing inside what is tested; the built-in
// themes sit well inside them.
constexpr double kMaxRadius = 100;
constexpr double kMinControlSize = 16, kMaxControlSize = 96;
constexpr double kMinFontSize = 6, kMaxFontSize = 96;
constexpr double kMaxDurationMs = 5000;
// Contrast k maps glyph coverage c to c(1 + k) / (1 + kc), which needs k > -1; past these ends
// text turns to a blur or a stencil.
constexpr double kMinContrast = -0.5, kMaxContrast = 2;

struct KindInfo {
  TokenKind kind;
  std::string_view noun;  // for messages: "a colour"
  double min = 0, max = 0;
};

constexpr KindInfo kKinds[] = {
    {TokenKind::color, "a colour"},
    {TokenKind::radius, "a radius", 0, kMaxRadius},
    {TokenKind::control_size, "a control size", kMinControlSize, kMaxControlSize},
    {TokenKind::font_size, "a font size", kMinFontSize, kMaxFontSize},
    {TokenKind::duration, "a duration", 0, kMaxDurationMs},
    {TokenKind::flag, "true or false"},
    {TokenKind::blend, "a text blend"},
    {TokenKind::contrast, "a contrast", kMinContrast, kMaxContrast},
};
static_assert([] {
  for (size_t i = 0; i < std::size(kKinds); ++i) {
    if (static_cast<size_t>(kKinds[i].kind) != i) return false;
  }
  return true;
}());

const KindInfo& kind_info(TokenKind kind) { return kKinds[static_cast<size_t>(kind)]; }

constexpr std::array<std::string_view, 3> kBlendWords = {"adaptive", "srgb", "linear"};
constexpr std::array<TextBlend, 3> kBlends = {TextBlend::adaptive, TextBlend::srgb,
                                              TextBlend::linear};

std::string format_number(double v) {
  char buf[32];
  const auto [end, ec] = std::to_chars(buf, buf + sizeof buf, v);
  return ec == std::errc{} ? std::string(buf, end) : std::string("?");
}

char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

// Edits (insert, delete, change, swap two neighbours) between a and b, ignoring case.
size_t edit_distance(std::string_view a, std::string_view b) {
  std::vector<size_t> before(b.size() + 1), prev(b.size() + 1), cur(b.size() + 1);
  for (size_t j = 0; j <= b.size(); ++j) prev[j] = j;
  for (size_t i = 1; i <= a.size(); ++i) {
    cur[0] = i;
    for (size_t j = 1; j <= b.size(); ++j) {
      const bool same = lower(a[i - 1]) == lower(b[j - 1]);
      cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (same ? 0 : 1)});
      if (i > 1 && j > 1 && lower(a[i - 1]) == lower(b[j - 2]) &&
          lower(a[i - 2]) == lower(b[j - 1])) {
        cur[j] = std::min(cur[j], before[j - 2] + 1);
      }
    }
    std::swap(before, prev);
    std::swap(prev, cur);
  }
  return prev[b.size()];
}

// What a token takes, for messages: "a colour: #rrggbb, ...".
std::string takes(const TokenSpec& spec) {
  const KindInfo& k = kind_info(spec.kind);
  std::string out = quote_text(spec.name) + " takes ";
  switch (spec.kind) {
    case TokenKind::color:
      return out + "a colour: \"#rrggbb\", \"#rrggbbaa\", a colour token, or either with an " +
             "opacity, such as \"accent 18%\"";
    case TokenKind::duration:
      return out + "a duration in whole milliseconds, such as \"120ms\" (0 to " +
             format_number(k.max) + "), or the name of another such token";
    case TokenKind::flag:
      return out + "true or false, or the name of another such token";
    case TokenKind::blend:
      return out + "\"adaptive\", \"srgb\" or \"linear\"";
    default:
      return out + std::string(k.noun) + " from " + format_number(k.min) + " to " +
             format_number(k.max) + ", or the name of another such token";
  }
}

std::string_view trim(std::string_view s) {
  while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
  while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
  return s;
}

int hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// "#rrggbb" or "#rrggbbaa".
std::optional<Color> parse_hex(std::string_view s) {
  constexpr size_t kRgbLength = 7, kRgbaLength = 9;
  if (s.size() != kRgbLength && s.size() != kRgbaLength) return std::nullopt;
  uint32_t v = 0;
  for (const char c : s.substr(1)) {
    const int d = hex_digit(c);
    if (d < 0) return std::nullopt;
    v = v << 4 | static_cast<uint32_t>(d);
  }
  if (s.size() == kRgbLength) return Color::hex(v);
  return Color::hex(v >> 8, static_cast<float>(v & 0xFF) / 255.0f);
}

std::optional<TokenExpr> read_reference(const TokenSpec& spec, std::string_view name, TextPos pos,
                                        ThemeErrors& errors) {
  const std::optional<size_t> i = find_token(name);
  if (!i) {
    std::vector<std::string_view> candidates = token_names(spec.kind);
    if (spec.kind == TokenKind::blend) candidates.assign(kBlendWords.begin(), kBlendWords.end());
    std::string message = "unknown token " + quote_text(name) + did_you_mean(name, candidates);
    double ignored = 0;
    const auto [end, ec] = std::from_chars(name.data(), name.data() + name.size(), ignored);
    if (spec.kind != TokenKind::color && ec == std::errc{} && end == name.data() + name.size()) {
      message += "; numbers are written without quotes";
    }
    errors.add(pos, message);
    return std::nullopt;
  }
  if (kTokens[*i].kind != spec.kind) {
    errors.add(pos, takes(spec) + "; " + quote_text(name) + " is " +
                        std::string(kind_info(kTokens[*i].kind).noun));
    return std::nullopt;
  }
  TokenExpr e;
  e.ref = static_cast<int>(*i);
  return e;
}

std::optional<TokenExpr> read_color(const TokenSpec& spec, std::string_view s, TextPos pos,
                                    ThemeErrors& errors) {
  s = trim(s);
  const size_t space = s.find(' ');
  const std::string_view head = s.substr(0, space);
  float opacity = 1;
  if (space != std::string_view::npos) {
    const std::string_view tail = trim(s.substr(space));
    const std::optional<float> p = parse_percent(tail);
    if (!p) {
      errors.add(pos,
                 "expected an opacity from 0% to 100% after the colour, found " + quote_text(tail));
      return std::nullopt;
    }
    opacity = *p;
  }
  if (head.starts_with('#')) {
    const std::optional<Color> c = parse_hex(head);
    if (!c) {
      errors.add(pos, quote_text(head) + " is not a colour; write \"#rrggbb\" or \"#rrggbbaa\"");
      return std::nullopt;
    }
    TokenExpr e;
    e.literal.color = c->with_alpha(c->a * opacity);
    return e;
  }
  std::optional<TokenExpr> e = read_reference(spec, head, pos, errors);
  if (e) e->opacity = opacity;
  return e;
}

// "120ms".
std::optional<TokenExpr> read_duration(const TokenSpec& spec, std::string_view s, TextPos pos,
                                       ThemeErrors& errors) {
  if (s.front() != '-' && (s.front() < '0' || s.front() > '9')) {
    return read_reference(spec, s, pos, errors);
  }
  long long ms = 0;
  const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), ms);
  if (ec != std::errc{} || std::string_view(end, s.data() + s.size() - end) != "ms" || ms < 0 ||
      ms > kMaxDurationMs) {
    errors.add(pos, takes(spec) + "; got " + quote_text(s));
    return std::nullopt;
  }
  TokenExpr e;
  e.literal.number = static_cast<double>(ms);
  return e;
}

}  // namespace

// Divided in float so that a whole percentage gives exactly the float a C++ literal would (18%
// is 0.18f).
std::optional<float> parse_percent(std::string_view s) {
  if (s.size() < 2 || s.back() != '%') return std::nullopt;
  s.remove_suffix(1);
  double p = 0;
  const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), p);
  if (ec != std::errc{} || end != s.data() + s.size() || !(p >= 0 && p <= 100)) {
    return std::nullopt;
  }
  return static_cast<float>(p) / 100.0f;
}

std::optional<TokenExpr> read_token_value(const TokenSpec& spec, const JsonValue& v,
                                          ThemeErrors& errors) {
  const KindInfo& k = kind_info(spec.kind);
  const bool is_string = v.kind == JsonKind::string;
  if (is_string && trim(v.string).empty()) {
    errors.add(v.pos, takes(spec) + "; got an empty string");
    return std::nullopt;
  }
  switch (spec.kind) {
    case TokenKind::color:
      if (is_string) return read_color(spec, v.string, v.pos, errors);
      break;
    case TokenKind::duration:
      if (is_string) return read_duration(spec, v.string, v.pos, errors);
      break;
    case TokenKind::flag:
      if (v.kind == JsonKind::boolean) {
        TokenExpr e;
        e.literal.flag = v.boolean;
        return e;
      }
      if (is_string) return read_reference(spec, v.string, v.pos, errors);
      break;
    case TokenKind::blend:
      if (is_string) {
        if (const std::optional<size_t> i = find_name(kBlendWords, v.string)) {
          TokenExpr e;
          e.literal.blend = kBlends[*i];
          return e;
        }
        return read_reference(spec, v.string, v.pos, errors);
      }
      break;
    default:  // the numbers
      if (v.kind == JsonKind::number) {
        if (!(v.number >= k.min && v.number <= k.max)) {
          errors.add(v.pos, takes(spec) + "; got " + format_number(v.number));
          return std::nullopt;
        }
        TokenExpr e;
        e.literal.number = v.number;
        return e;
      }
      if (is_string) return read_reference(spec, v.string, v.pos, errors);
      break;
  }
  errors.add(v.pos, takes(spec) + "; got " + json_kind_name(v.kind));
  return std::nullopt;
}

// Every token a theme file can set. constinit: a theme may be read during static
// initialisation, so the table cannot wait for dynamic initialisation.
constinit const TokenSpec kTokens[] = {
    {"surface.titlebar", TokenKind::color, &Theme::titlebar},
    {"surface.sidebar", TokenKind::color, &Theme::sidebar},
    {"surface.background", TokenKind::color, &Theme::background},
    {"surface.raised", TokenKind::color, &Theme::raised},
    {"line.border", TokenKind::color, &Theme::border},
    {"line.divider", TokenKind::color, &Theme::divider},
    {"state.hover", TokenKind::color, &Theme::hover},
    {"state.selected", TokenKind::color, &Theme::selected},
    {"text.primary", TokenKind::color, &Theme::text},
    {"text.secondary", TokenKind::color, &Theme::text_secondary},
    {"text.muted", TokenKind::color, &Theme::text_muted},
    {"accent", TokenKind::color, &Theme::accent},
    {"accent.hover", TokenKind::color, &Theme::accent_hover},
    {"accent.on", TokenKind::color, &Theme::on_accent},
    {"accent.soft", TokenKind::color, &Theme::accent_soft},
    {"status.success", TokenKind::color, &Theme::success},
    {"status.warning", TokenKind::color, &Theme::warning},
    {"status.danger", TokenKind::color, &Theme::danger},
    {"depth.shadows", TokenKind::flag, &Theme::shadows},
    {"depth.shadow", TokenKind::color, &Theme::shadow},
    {"depth.highlight", TokenKind::color, &Theme::top_highlight},
    {"radius.control", TokenKind::radius, &Theme::radius_control},
    {"radius.card", TokenKind::radius, &Theme::radius_card},
    {"size.control", TokenKind::control_size, &Theme::control_height},
    {"font.caption", TokenKind::font_size, &Theme::text_caption},
    {"font.body", TokenKind::font_size, &Theme::text_body},
    {"font.title", TokenKind::font_size, &Theme::text_title},
    {"font.heading", TokenKind::font_size, &Theme::text_heading},
    {"motion.fast", TokenKind::duration, &Theme::motion_fast},
    {"motion.base", TokenKind::duration, &Theme::motion_base},
    {"text.blend", TokenKind::blend, &TextTuning::blend},
    {"text.contrast", TokenKind::contrast, &TextTuning::contrast},
    {"text.snap", TokenKind::flag, &TextTuning::snap_x_height},
    {"dark", TokenKind::flag, &Theme::dark},
};

std::optional<size_t> find_token(std::string_view name) {
  for (size_t i = 0; i < kTokenCount; ++i) {
    if (kTokens[i].name == name) return i;
  }
  return std::nullopt;
}

std::vector<std::string_view> token_names(std::optional<TokenKind> only) {
  std::vector<std::string_view> names;
  for (const TokenSpec& t : kTokens) {
    if (!only || t.kind == *only) names.push_back(t.name);
  }
  return names;
}

std::string quote_text(std::string_view s) {
  std::string out = "\"";
  size_t n = std::min(s.size(), kMaxQuoted);
  while (n < s.size() && n > 0 && (static_cast<uint8_t>(s[n]) & 0xC0) == 0x80) --n;
  for (const char c : s.substr(0, n)) out += static_cast<uint8_t>(c) < 0x20 ? '?' : c;
  if (n < s.size()) out += "...";
  return out + "\"";
}

std::string set_twice(std::string_view key, TextPos first) {
  return quote_text(key) + " is set twice; it was first set on line " + std::to_string(first.line);
}

std::string did_you_mean(std::string_view typed, std::span<const std::string_view> candidates) {
  std::string_view best;
  size_t best_distance = SIZE_MAX;
  for (const std::string_view c : candidates) {
    const size_t limit = std::max<size_t>(1, c.size() / 3);
    const size_t gap = typed.size() > c.size() ? typed.size() - c.size() : c.size() - typed.size();
    if (gap > limit) continue;
    const size_t d = edit_distance(typed, c);
    if (d <= limit && d < best_distance) best = c, best_distance = d;
  }
  return best.empty() ? std::string() : "; did you mean " + quote_text(best) + "?";
}

}  // namespace smalti::detail
