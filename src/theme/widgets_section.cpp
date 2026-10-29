// The "widgets" section: each key names a widget and optionally a variant ("Button.primary") and
// holds properties and state blocks. Every name is checked against the widgets' schema, so a
// mistake is reported where it is, with what would have been valid.
#include "theme/widgets_section.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace smalti::detail {

namespace {

// "a, b and c": what is allowed, for a message.
std::string listed(std::span<const std::string_view> names) {
  std::string out;
  for (size_t i = 0; i < names.size(); ++i) {
    if (i > 0) out += i + 1 == names.size() ? " and " : ", ";
    out += names[i];
  }
  return out;
}

std::vector<std::string_view> widget_names() {
  std::vector<std::string_view> names;
  for (const WidgetSchema& s : widget_schemas()) names.push_back(s.name);
  return names;
}

std::vector<std::string_view> state_names(const WidgetSchema& schema) {
  std::vector<std::string_view> names;
  for (size_t i = 1; i < kStyleStateCount; ++i) {
    if (schema.states & state_bit(static_cast<StyleState>(i))) names.push_back(kStyleStateNames[i]);
  }
  return names;
}

// The properties a block may set in `state`, and at rest the widget's states as well.
std::vector<std::string_view> block_names(const WidgetSchema& schema, StyleState state) {
  std::vector<std::string_view> names(schema.colors.begin(), schema.colors.end());
  if (schema.radius && state == StyleState::rest) names.push_back("radius");
  if (state == StyleState::disabled) names.push_back("opacity");
  if (state == StyleState::rest) {
    for (const std::string_view s : state_names(schema)) names.push_back(s);
  }
  return names;
}

// Keys already read in one object, to report a key set twice. Only valid keys are kept, so it
// stays as small as the schema.
class SeenKeys {
 public:
  // False, with the error reported, if the key was set before.
  bool first(const JsonMember& m, ThemeErrors& errors) {
    for (const auto& [key, pos] : seen_) {
      if (key == m.key) {
        errors.add(m.pos, set_twice(m.key, pos));
        return false;
      }
    }
    seen_.emplace_back(m.key, m.pos);
    return true;
  }

 private:
  std::vector<std::pair<std::string_view, TextPos>> seen_;
};

class SectionReader {
 public:
  SectionReader(std::vector<PendingStyleRule>& out, ThemeErrors& errors)
      : out_(out), errors_(errors) {}

  void read(const JsonValue& widgets) {
    if (widgets.kind != JsonKind::object) {
      errors_.add(widgets.pos, "\"widgets\" is an object of widget names and their styles, not " +
                                   std::string(json_kind_name(widgets.kind)));
      return;
    }
    SeenKeys seen;
    for (const JsonMember& m : widgets.members) {
      if (read_key(m) && seen.first(m, errors_)) read_block(StyleState::rest, m.value);
    }
  }

 private:
  // "Button" or "Button.primary"; false, with the error reported, if it names nothing.
  bool read_key(const JsonMember& m) {
    const size_t dot = m.key.find('.');
    const std::string_view name = std::string_view(m.key).substr(0, dot);
    const std::optional<StyledWidget> w = find_styled_widget(name);
    if (!w) {
      const std::vector<std::string_view> names = widget_names();
      errors_.add(m.pos, "unknown widget " + quote_text(name) + did_you_mean(name, names) +
                             "; the widgets are " + listed(names));
      return false;
    }
    schema_ = &schema_of(*w);
    variant_ = {};
    key_ = m.key;
    if (dot == std::string::npos) return true;
    const std::string_view variant = std::string_view(m.key).substr(dot + 1);
    if (schema_->variants.empty()) {
      errors_.add(m.pos, quote_text(schema_->name) + " has no variants");
      return false;
    }
    if (!find_variant(*schema_, variant)) {
      errors_.add(m.pos, "unknown variant " + quote_text(variant) + " of " +
                             quote_text(schema_->name) + did_you_mean(variant, schema_->variants) +
                             "; its variants are " + listed(schema_->variants));
      return false;
    }
    variant_ = variant;
    return true;
  }

  void read_block(StyleState state, const JsonValue& block) {
    if (block.kind != JsonKind::object) {
      errors_.add(block.pos, quote_text(key_) + " is an object of properties and states, not " +
                                 std::string(json_kind_name(block.kind)));
      return;
    }
    SeenKeys seen;
    for (const JsonMember& m : block.members) {
      const std::optional<StyleState> s = find_style_state(m.key);
      // Only names the schema knows are remembered: a block of thousands of unknown names
      // costs one report each, not a comparison with every name before it.
      const bool known = s || find_property(*schema_, state, m.key);
      if (known && !seen.first(m, errors_)) continue;
      if (s) {
        read_state(state, *s, m);
      } else {
        read_property(state, m);
      }
    }
  }

  void read_state(StyleState outer, StyleState state, const JsonMember& m) {
    if (outer != StyleState::rest) {
      errors_.add(m.pos, quote_text(m.key) + " cannot go inside " +
                             quote_text(kStyleStateNames[static_cast<size_t>(outer)]) +
                             "; states are set side by side, at the top of a widget's style");
      return;
    }
    if (!(schema_->states & state_bit(state))) {
      const std::vector<std::string_view> states = state_names(*schema_);
      errors_.add(m.pos, quote_text(schema_->name) + " has no " + quote_text(m.key) + " state" +
                             (states.empty() ? "; it has no states"
                                             : "; its states are " + listed(states)));
      return;
    }
    read_block(state, m.value);
  }

  void read_property(StyleState state, const JsonMember& m) {
    const std::optional<StyleProperty> p = find_property(*schema_, state, m.key);
    if (!p) {
      report_unknown_property(state, m);
      return;
    }
    PendingStyleRule pending;
    pending.rule.widget = schema_->name;
    pending.rule.variant = variant_;
    pending.rule.state = kStyleStateNames[static_cast<size_t>(state)];
    pending.rule.property = m.key;
    pending.kind = p->kind;
    if (p->kind == StyleProperty::Kind::opacity) {
      const std::optional<float> opacity =
          m.value.kind == JsonKind::string ? parse_percent(m.value.string) : std::nullopt;
      if (!opacity) {
        errors_.add(m.value.pos, "\"opacity\" takes a percentage from 0% to 100%, such as \"45%\"");
        return;
      }
      pending.opacity = *opacity;
    } else {
      // A colour or a radius is written as a token's value is, and may name a token.
      const bool color = p->kind == StyleProperty::Kind::color;
      const TokenSpec spec{m.key, color ? TokenKind::color : TokenKind::radius,
                           color ? ThemeField{&Theme::accent} : ThemeField{&Theme::radius_card}};
      std::optional<TokenExpr> e = read_token_value(spec, m.value, errors_);
      if (!e) return;
      e->pos = m.value.pos;
      pending.expr = *e;
    }
    out_.push_back(std::move(pending));
  }

  void report_unknown_property(StyleState state, const JsonMember& m) {
    const std::string_view where = kStyleStateNames[static_cast<size_t>(state)];
    if (m.key == "radius" && schema_->radius) {
      errors_.add(m.pos, "\"radius\" is set at the top of a widget's style, not under " +
                             quote_text(where));
    } else if (m.key == "opacity") {
      errors_.add(m.pos, "\"opacity\" is set under \"disabled\"");
    } else {
      const std::vector<std::string_view> names = block_names(*schema_, state);
      errors_.add(
          m.pos,
          "unknown property " + quote_text(m.key) + " of " + quote_text(schema_->name) +
              did_you_mean(m.key, names) + "; " +
              (state == StyleState::rest ? "it has " : "under " + quote_text(where) + " it has ") +
              listed(names));
    }
  }

  std::vector<PendingStyleRule>& out_;
  ThemeErrors& errors_;
  const WidgetSchema* schema_ = nullptr;  // of the widget being read
  std::string_view variant_;
  std::string_view key_;
};

}  // namespace

void read_widgets_section(const JsonValue& widgets, std::vector<PendingStyleRule>& out,
                          ThemeErrors& errors) {
  SectionReader(out, errors).read(widgets);
}

}  // namespace smalti::detail
