// Theme files: reading a whole file (tokens.cpp reads each value), resolving the references
// between tokens, and the built-in themes, which are theme files compiled into the library.
#include "smalti/theme.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <type_traits>
#include <variant>

#include "smalti/ref.hpp"
#include "theme/jsonc.hpp"
#include "theme/tokens.hpp"
#include "theme/widgets_section.hpp"

namespace smalti {

namespace detail {
// Generated from themes/*.jsonc by cmake/embed_files.cmake.
extern const unsigned char kThemeDark[];
extern const size_t kThemeDarkSize;
extern const unsigned char kThemeBlack[];
extern const size_t kThemeBlackSize;
extern const unsigned char kThemeLight[];
extern const size_t kThemeLightSize;
}  // namespace detail

namespace {

using detail::did_you_mean;
using detail::find_name;
using detail::find_token;
using detail::json_kind_name;
using detail::JsonKind;
using detail::JsonMember;
using detail::JsonValue;
using detail::kTokenCount;
using detail::kTokens;
using detail::PendingStyleRule;
using detail::quote_text;
using detail::read_token_value;
using detail::set_twice;
using detail::TextPos;
using detail::ThemeErrors;
using detail::ThemeField;
using detail::token_names;
using detail::TokenExpr;
using detail::TokenValue;

enum class Base : uint8_t { none, dark, black, light };
constexpr std::array<std::string_view, 4> kBaseNames = {"none", "dark", "black", "light"};

enum class TopKey : uint8_t { name, base, tokens, widgets };
constexpr std::array<std::string_view, 4> kTopKeys = {"name", "base", "tokens", "widgets"};

struct Document {
  std::optional<std::string> name;
  Base base = Base::dark;
  TextPos tokens_pos;
  std::array<std::optional<TokenExpr>, kTokenCount> tokens;
  std::array<bool, kTokenCount> listed{};  // the file has the key, valid or not
  std::vector<PendingStyleRule> rules;     // the "widgets" section
};

void read_tokens(const JsonValue& tokens, Document& doc, ThemeErrors& errors) {
  if (tokens.kind != JsonKind::object) {
    errors.add(tokens.pos, "\"tokens\" is an object of token names and values, not " +
                               std::string(json_kind_name(tokens.kind)));
    return;
  }
  std::array<std::optional<TextPos>, kTokenCount> seen;
  for (const JsonMember& m : tokens.members) {
    const std::optional<size_t> i = find_token(m.key);
    if (!i) {
      errors.add(m.pos, "unknown token " + quote_text(m.key) + did_you_mean(m.key, token_names()));
      continue;
    }
    if (seen[*i]) {
      errors.add(m.pos, set_twice(m.key, *seen[*i]));
      continue;
    }
    seen[*i] = m.pos;
    doc.listed[*i] = true;
    std::optional<TokenExpr> e = read_token_value(kTokens[*i], m.value, errors);
    if (!e) continue;
    e->pos = m.value.pos;
    e->in_file = true;
    doc.tokens[*i] = *e;
  }
}

void read_member(TopKey key, const JsonMember& m, Document& doc, ThemeErrors& errors) {
  const JsonValue& v = m.value;
  switch (key) {
    case TopKey::name:
      if (v.kind == JsonKind::string) {
        doc.name = v.string;
      } else {
        errors.add(v.pos, "\"name\" is a string, not " + std::string(json_kind_name(v.kind)));
      }
      return;
    case TopKey::base:
      if (v.kind == JsonKind::string) {
        if (const std::optional<size_t> b = find_name(kBaseNames, v.string)) {
          doc.base = static_cast<Base>(*b);
          return;
        }
      }
      errors.add(v.pos, "\"base\" is \"dark\", \"black\", \"light\" or \"none\"" +
                            (v.kind == JsonKind::string ? "; got " + quote_text(v.string) +
                                                              did_you_mean(v.string, kBaseNames)
                                                        : std::string()));
      return;
    case TopKey::tokens:
      doc.tokens_pos = m.pos;
      read_tokens(v, doc, errors);
      return;
    case TopKey::widgets:
      detail::read_widgets_section(v, doc.rules, errors);
      return;
  }
}

Document read_document(const JsonValue& root, ThemeErrors& errors) {
  Document doc;
  if (root.kind != JsonKind::object) {
    errors.add(root.pos, "a theme is an object, { \"tokens\": { ... } }, not " +
                             std::string(json_kind_name(root.kind)));
    return doc;
  }
  std::array<std::optional<TextPos>, kTopKeys.size()> seen;
  for (const JsonMember& m : root.members) {
    const std::optional<size_t> key = find_name(kTopKeys, m.key);
    if (!key) {
      errors.add(m.pos, "unknown key " + quote_text(m.key) + did_you_mean(m.key, kTopKeys) +
                            "; a theme has \"name\", \"base\", \"tokens\" and \"widgets\"");
      continue;
    }
    if (seen[*key]) {
      errors.add(m.pos, set_twice(m.key, *seen[*key]));
      continue;
    }
    seen[*key] = m.pos;
    read_member(static_cast<TopKey>(*key), m, doc, errors);
  }
  return doc;
}

const Document& builtin_document(Base base);

// Works out every token's value, following references; reports loops once each.
class Resolver {
 public:
  Resolver(const std::array<const TokenExpr*, kTokenCount>& exprs, ThemeErrors& errors)
      : exprs_(exprs), errors_(errors) {}

  bool resolve_all() {
    bool ok = true;
    for (size_t i = 0; i < kTokenCount; ++i) ok = resolve(i) && ok;
    return ok;
  }
  const TokenValue& value(size_t i) const { return values_[i]; }

 private:
  enum class State : uint8_t { unvisited, visiting, resolved, failed };

  bool resolve(size_t i) {
    if (state_[i] == State::resolved) return true;
    if (state_[i] == State::failed) return false;
    if (state_[i] == State::visiting) {
      report_loop(i);
      return false;
    }
    const TokenExpr& e = *exprs_[i];
    state_[i] = State::visiting;
    stack_.push_back(i);
    bool ok = true;
    if (e.ref < 0) {
      values_[i] = e.literal;
    } else if (resolve(static_cast<size_t>(e.ref))) {
      values_[i] = values_[static_cast<size_t>(e.ref)];
      values_[i].color.a *= e.opacity;
    } else {
      ok = false;
    }
    stack_.pop_back();
    // A token on a reported loop is already failed; leave it so.
    if (state_[i] == State::visiting) state_[i] = ok ? State::resolved : State::failed;
    return ok;
  }

  // The loop runs from token `i`, on the stack, to the top of the stack and back to `i`.
  void report_loop(size_t i) {
    const auto start = std::find(stack_.begin(), stack_.end(), i);
    std::string path;
    TextPos at = exprs_[i]->pos;
    bool placed = false;
    for (auto it = start; it != stack_.end(); ++it) {
      path += std::string(kTokens[*it].name) + " -> ";
      if (!placed && exprs_[*it]->in_file) at = exprs_[*it]->pos, placed = true;
      state_[*it] = State::failed;
    }
    path += kTokens[i].name;
    errors_.add(at, "these tokens refer to each other in a loop: " + path);
  }

  const std::array<const TokenExpr*, kTokenCount>& exprs_;
  ThemeErrors& errors_;
  std::array<State, kTokenCount> state_{};
  std::array<TokenValue, kTokenCount> values_{};
  std::vector<size_t> stack_;
};

void set_field(Theme& theme, const ThemeField& field, const TokenValue& v) {
  std::visit(
      [&](auto member) {
        using M = decltype(member);
        if constexpr (std::is_same_v<M, Color Theme::*>) {
          theme.*member = v.color;
        } else if constexpr (std::is_same_v<M, float Theme::*>) {
          theme.*member = static_cast<float>(v.number);
        } else if constexpr (std::is_same_v<M, bool Theme::*>) {
          theme.*member = v.flag;
        } else if constexpr (std::is_same_v<M, std::chrono::milliseconds Theme::*>) {
          theme.*member = std::chrono::milliseconds(static_cast<long long>(v.number));
        } else if constexpr (std::is_same_v<M, TextBlend TextTuning::*>) {
          theme.text_tuning.*member = v.blend;
        } else if constexpr (std::is_same_v<M, float TextTuning::*>) {
          theme.text_tuning.*member = static_cast<float>(v.number);
        } else {
          static_assert(std::is_same_v<M, bool TextTuning::*>);
          theme.text_tuning.*member = v.flag;
        }
      },
      field);
}

// A widget style rule's value, now that the tokens it may name are known.
StyleRule resolve_rule(const PendingStyleRule& p, const Resolver& tokens) {
  StyleRule r = p.rule;
  TokenValue v = p.expr.literal;
  if (p.expr.ref >= 0) {
    v = tokens.value(static_cast<size_t>(p.expr.ref));
    v.color.a *= p.expr.opacity;
  }
  switch (p.kind) {
    case detail::StyleProperty::Kind::color:
      r.color = v.color;
      break;
    case detail::StyleProperty::Kind::radius:
      r.number = static_cast<float>(v.number);
      break;
    case detail::StyleProperty::Kind::opacity:
      r.number = p.opacity;
      break;
  }
  return r;
}

// "night.jsonc" or "themes/night.jsonc" gives "night".
std::string stem(std::string_view source) {
  const size_t slash = source.find_last_of("/\\");
  if (slash != std::string_view::npos) source.remove_prefix(slash + 1);
  const size_t dot = source.rfind('.');
  if (dot != std::string_view::npos && dot > 0) source = source.substr(0, dot);
  return std::string(source);
}

// The file's tokens over its base's; nullopt (with the reasons in `errors`) if any is wrong.
std::optional<Theme> build(const Document& doc, std::string_view source, ThemeErrors& errors) {
  const Document* base = doc.base == Base::none ? nullptr : &builtin_document(doc.base);
  std::array<const TokenExpr*, kTokenCount> exprs{};
  std::string missing;
  for (size_t i = 0; i < kTokenCount; ++i) {
    if (doc.tokens[i]) {
      exprs[i] = &*doc.tokens[i];
    } else if (base && base->tokens[i]) {
      exprs[i] = &*base->tokens[i];
    } else if (!doc.listed[i]) {
      missing += (missing.empty() ? "" : ", ") + std::string(kTokens[i].name);
    }
  }
  if (!missing.empty()) {
    errors.add(doc.tokens_pos,
               "\"base\": \"none\" needs every token, and these are not set: " + missing);
  }
  if (errors.any()) return std::nullopt;
  Resolver resolver(exprs, errors);
  if (!resolver.resolve_all()) return std::nullopt;
  Theme theme;
  for (size_t i = 0; i < kTokenCount; ++i) set_field(theme, kTokens[i].field, resolver.value(i));
  theme.name = doc.name ? *doc.name : stem(source);
  for (const PendingStyleRule& p : doc.rules)
    theme.widget_styles.push_back(resolve_rule(p, resolver));
  return theme;
}

std::string_view builtin_text(Base base) {
  const auto view = [](const unsigned char* data, size_t size) {
    return std::string_view(reinterpret_cast<const char*>(data), size);
  };
  switch (base) {
    case Base::black:
      return view(detail::kThemeBlack, detail::kThemeBlackSize);
    case Base::light:
      return view(detail::kThemeLight, detail::kThemeLightSize);
    default:
      return view(detail::kThemeDark, detail::kThemeDarkSize);
  }
}

[[noreturn]] void broken_builtin(Base base, const ThemeError& e) {
  const std::string message = "the built-in theme " +
                              std::string(kBaseNames[static_cast<size_t>(base)]) +
                              ".jsonc has an error at line " + std::to_string(e.line) +
                              ", column " + std::to_string(e.column) + ": " + e.message;
  detail::contract_violation(message.c_str());
}

struct Builtin {
  Document document;
  Theme theme;
};

// Each built-in theme is checked whole the first time any is used: it must parse, set every
// token itself ("base": "none", so this never recurses), and resolve.
const Builtin& builtin(Base base) {
  static const std::array<Builtin, 3> all = [] {
    std::array<Builtin, 3> out;
    for (const Base b : {Base::dark, Base::black, Base::light}) {
      Builtin& slot = out[static_cast<size_t>(b) - 1];
      const detail::JsonParse parsed = detail::parse_jsonc(builtin_text(b));
      if (parsed.error) {
        broken_builtin(b,
                       {parsed.error->pos.line, parsed.error->pos.column, parsed.error->message});
      }
      ThemeErrors errors;
      slot.document = read_document(parsed.root, errors);
      if (!errors.any() && slot.document.base != Base::none) {
        errors.add({}, "a built-in theme sets every token itself: \"base\": \"none\"");
      }
      std::optional<Theme> theme;
      if (!errors.any()) theme = build(slot.document, {}, errors);
      if (!theme) broken_builtin(b, errors.take().front());
      slot.theme = std::move(*theme);
    }
    return out;
  }();
  return all[static_cast<size_t>(base) - 1];
}

const Document& builtin_document(Base base) { return builtin(base).document; }
const Theme& builtin_theme(Base base) { return builtin(base).theme; }

ThemeLoad file_error(std::string message) {
  ThemeLoad out;
  out.theme = builtin_theme(Base::dark);
  out.errors.push_back({0, 0, std::move(message)});
  return out;
}

}  // namespace

const Theme& theme_dark() { return builtin_theme(Base::dark); }
const Theme& theme_black() { return builtin_theme(Base::black); }
const Theme& theme_light() { return builtin_theme(Base::light); }

ThemeLoad parse_theme(std::string_view text, std::string_view source) {
  ThemeLoad out;
  const detail::JsonParse parsed = detail::parse_jsonc(text);
  if (parsed.error) {
    out.theme = theme_dark();
    out.errors.push_back({parsed.error->pos.line, parsed.error->pos.column, parsed.error->message});
    return out;
  }
  ThemeErrors errors;
  const Document doc = read_document(parsed.root, errors);
  std::optional<Theme> theme = build(doc, source, errors);
  if (theme) {
    out.theme = std::move(*theme);
  } else {
    out.theme = builtin_theme(doc.base == Base::none ? Base::dark : doc.base);
    out.errors = errors.take();
  }
  return out;
}

ThemeLoad load_theme(const std::filesystem::path& path) {
  const std::u8string shown8 = path.u8string();
  const std::string shown(shown8.begin(), shown8.end());
  std::error_code ec;
  const std::filesystem::file_status status = std::filesystem::status(path, ec);
  if (status.type() == std::filesystem::file_type::not_found) {
    return file_error("there is no file " + quote_text(shown));
  }
  if (status.type() == std::filesystem::file_type::directory) {
    return file_error(quote_text(shown) + " is a folder, not a theme file");
  }
  const uintmax_t size = std::filesystem::file_size(path, ec);
  if (ec) return file_error("cannot read " + quote_text(shown));
  if (size > detail::kMaxJsonBytes) {
    return file_error(quote_text(shown) + " is larger than " +
                      std::to_string(detail::kMaxJsonBytes >> 20) + " MB");
  }
  std::string text(static_cast<size_t>(size), '\0');
  std::ifstream in(path, std::ios::binary);
  if (!in.read(text.data(), static_cast<std::streamsize>(text.size()))) {
    return file_error("cannot read " + quote_text(shown));
  }
  return parse_theme(text, shown);
}

}  // namespace smalti
