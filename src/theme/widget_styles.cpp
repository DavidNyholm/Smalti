// Widget styles: the schema of the "widgets" section, the built-in look of every widget as the
// tokens give it (exactly what 0.1 drew), and the theme's rules applied over it.
#include "theme/widget_styles.hpp"

#include <string>
#include <vector>

#include "smalti/ref.hpp"
#include "smalti/widgets.hpp"

namespace smalti::detail {

namespace {

constexpr std::string_view kButtonVariants[] = {"primary", "secondary", "ghost", "danger"};
constexpr std::string_view kLabelVariants[] = {"body", "secondary", "caption", "title", "heading"};

constexpr std::string_view kButtonColors[] = {"fill", "border", "text", "focus"};
constexpr std::string_view kCheckBoxColors[] = {"fill", "border", "mark", "text", "focus"};
constexpr std::string_view kSwitchColors[] = {"track", "border", "knob", "text", "focus"};
constexpr std::string_view kRadioColors[] = {"fill", "border", "mark", "text", "focus"};
constexpr std::string_view kSliderColors[] = {"track", "fill", "knob", "border", "focus"};
constexpr std::string_view kProgressColors[] = {"track", "fill"};
constexpr std::string_view kSeparatorColors[] = {"fill"};
constexpr std::string_view kLabelColors[] = {"text"};
constexpr std::string_view kCardColors[] = {"fill", "border"};

constexpr uint8_t kButtonStates = state_bit(StyleState::hover) | state_bit(StyleState::pressed) |
                                  state_bit(StyleState::focused) | state_bit(StyleState::disabled);
constexpr uint8_t kToggleStates = state_bit(StyleState::hover) | state_bit(StyleState::checked) |
                                  state_bit(StyleState::focused) | state_bit(StyleState::disabled);
constexpr uint8_t kSliderStates =
    state_bit(StyleState::hover) | state_bit(StyleState::focused) | state_bit(StyleState::disabled);
constexpr uint8_t kDisabledOnly = state_bit(StyleState::disabled);

constexpr WidgetSchema kSchemas[] = {
    {"Button", kButtonVariants, kButtonColors, true, kButtonStates},
    {"CheckBox", {}, kCheckBoxColors, true, kToggleStates},
    {"Switch", {}, kSwitchColors, false, kToggleStates},
    {"RadioButton", {}, kRadioColors, false, kToggleStates},
    {"Slider", {}, kSliderColors, false, kSliderStates},
    {"ProgressBar", {}, kProgressColors, false, kDisabledOnly},
    {"Separator", {}, kSeparatorColors, false, 0},
    {"Label", kLabelVariants, kLabelColors, false, kDisabledOnly},
    {"Card", {}, kCardColors, true, 0},
};
static_assert(std::size(kSchemas) == kStyledWidgetCount);
static_assert(std::size(kButtonVariants) == static_cast<size_t>(ButtonVariant::danger) + 1);
static_assert(std::size(kLabelVariants) == static_cast<size_t>(TextRole::heading) + 1);

constexpr size_t variant_count(const WidgetSchema& s) {
  return s.variants.empty() ? 1 : s.variants.size();
}

static_assert([] {
  size_t n = 0;
  for (const WidgetSchema& s : kSchemas) {
    n += variant_count(s);
    if (s.colors.size() > kMaxStyleColors) return false;
  }
  return n == kStyledVariantCount;
}());

// The built-in look's constants, as 0.1 drew them.
constexpr Color kTransparent{0, 0, 0, 0};
// Secondary and ghost buttons tint by state.hover, half again as strong, under the pointer.
constexpr float kHoverTintStrength = 1.5f;
// A pressed button darkens by this much black over its fill.
constexpr Color kPressShade{0, 0, 0, 0.12f};
// A danger button's fill is its text colour at this opacity, and this much more under the pointer.
constexpr float kDangerFill = 0.14f, kDangerFillHoverGain = 0.06f;
constexpr float kCheckBoxRadius = 5;
// A switch's knob moves this far from its colour towards the text colour under the pointer.
constexpr float kSwitchKnobHoverMix = 0.5f;
constexpr Color kSliderKnob{1, 1, 1, 1};

size_t state_index(StyleState s) { return static_cast<size_t>(s); }

void set(WidgetStyle& s, size_t slot, StyleState state, Color c) {
  s.colors[slot].by_state[state_index(state)] = c;
}

Color rest(const WidgetStyle& s, size_t slot) {
  return *s.colors[slot].by_state[state_index(StyleState::rest)];
}

void button_defaults(StyleState stage, ButtonVariant v, const Theme& th, WidgetStyle& s) {
  using S = ButtonSlot;
  switch (stage) {
    case StyleState::rest: {
      s.radius = th.radius_control;
      const Color fill[] = {th.accent, th.raised, kTransparent, th.danger.with_alpha(kDangerFill)};
      const Color text[] = {th.on_accent, th.text, th.accent, th.danger};
      set(s, S::fill, stage, fill[static_cast<size_t>(v)]);
      set(s, S::text, stage, text[static_cast<size_t>(v)]);
      set(s, S::border, stage, v == ButtonVariant::secondary ? th.border : kTransparent);
      set(s, S::focus, stage, th.accent);
      return;
    }
    case StyleState::hover:
      if (v == ButtonVariant::primary) {
        set(s, S::fill, stage, th.accent_hover);
      } else if (v == ButtonVariant::danger) {
        set(s, S::fill, stage, th.danger.with_alpha(kDangerFill + kDangerFillHoverGain));
      } else {
        const Color tint = th.hover.with_alpha(th.hover.a * kHoverTintStrength);
        set(s, S::fill, stage, over(rest(s, S::fill), tint));
      }
      return;
    case StyleState::pressed: {
      const std::optional<Color>& hovered =
          s.colors[S::fill].by_state[state_index(StyleState::hover)];
      set(s, S::fill, stage, over(hovered ? *hovered : rest(s, S::fill), kPressShade));
      return;
    }
    default:
      return;
  }
}

// The defaults of `stage` for one widget variant. A default that builds on another state's
// colour (a hover tint, the press shade) reads that colour after the theme's rules for it.
void defaults(StyleState stage, StyledWidget w, size_t variant, const Theme& th, WidgetStyle& s) {
  const bool at_rest = stage == StyleState::rest;
  switch (w) {
    case StyledWidget::button:
      button_defaults(stage, static_cast<ButtonVariant>(variant), th, s);
      return;
    case StyledWidget::check_box: {
      using S = CheckBoxSlot;
      if (at_rest) {
        s.radius = kCheckBoxRadius;
        set(s, S::fill, stage, th.raised);
        set(s, S::border, stage, th.text_muted);
        set(s, S::mark, stage, th.on_accent);
        set(s, S::text, stage, th.text);
        set(s, S::focus, stage, th.accent);
      } else if (stage == StyleState::hover) {
        set(s, S::border, stage, th.text_secondary);
      } else if (stage == StyleState::checked) {
        set(s, S::fill, stage, th.accent);
        set(s, S::border, stage, kTransparent);
      }
      return;
    }
    case StyledWidget::switch_toggle: {
      using S = SwitchSlot;
      if (at_rest) {
        set(s, S::track, stage, th.selected);
        set(s, S::border, stage, th.border);
        set(s, S::knob, stage, th.text_secondary);
        set(s, S::text, stage, th.text);
        set(s, S::focus, stage, th.accent);
      } else if (stage == StyleState::hover) {
        set(s, S::knob, stage, Color::mix(rest(s, S::knob), th.text, kSwitchKnobHoverMix));
      } else if (stage == StyleState::checked) {
        set(s, S::track, stage, th.accent);
        set(s, S::border, stage, kTransparent);
        set(s, S::knob, stage, th.on_accent);
      }
      return;
    }
    case StyledWidget::radio_button: {
      using S = RadioSlot;
      if (at_rest) {
        set(s, S::fill, stage, th.raised);
        set(s, S::border, stage, th.text_muted);
        set(s, S::mark, stage, th.accent);
        set(s, S::text, stage, th.text);
        set(s, S::focus, stage, th.accent);
      } else if (stage == StyleState::hover) {
        set(s, S::border, stage, th.text_secondary);
      } else if (stage == StyleState::checked) {
        set(s, S::border, stage, th.accent);
      }
      return;
    }
    case StyledWidget::slider:
      if (at_rest) {
        set(s, SliderSlot::track, stage, th.selected);
        set(s, SliderSlot::fill, stage, th.accent);
        set(s, SliderSlot::knob, stage, kSliderKnob);
        set(s, SliderSlot::border, stage, th.border);
        set(s, SliderSlot::focus, stage, th.accent);
      }
      return;
    case StyledWidget::progress_bar:
      if (at_rest) {
        set(s, ProgressSlot::track, stage, th.selected);
        set(s, ProgressSlot::fill, stage, th.accent);
      }
      return;
    case StyledWidget::separator:
      if (at_rest) set(s, SeparatorSlot::fill, stage, th.divider);
      return;
    case StyledWidget::label:
      if (at_rest) {
        const Color text[] = {th.text, th.text_secondary, th.text_muted, th.text, th.text};
        set(s, LabelSlot::text, stage, text[variant]);
      }
      return;
    case StyledWidget::card:
      if (at_rest) {
        s.radius = th.radius_card;
        set(s, CardSlot::fill, stage, th.raised);
        set(s, CardSlot::border, stage, th.border);
      }
      return;
  }
}

[[noreturn]] void bad_rule(const StyleRule& r, std::string_view why) {
  const std::string message = "Theme::widget_styles has a rule for " + r.widget +
                              (r.variant.empty() ? "" : "." + r.variant) +
                              (r.state.empty() ? "" : " " + r.state) + " " + r.property + " that " +
                              std::string(why) + " (docs/Themes.md lists them)";
  contract_violation(message.c_str());
}

// Where a rule applies; a contract violation if it names nothing real.
struct RuleTarget {
  StyledWidget widget;
  std::optional<size_t> variant;  // nullopt: every variant
  StyleState state;
  StyleProperty property;
};

RuleTarget locate(const StyleRule& r) {
  RuleTarget t{};
  const std::optional<StyledWidget> w = find_styled_widget(r.widget);
  if (!w) bad_rule(r, "names no widget");
  t.widget = *w;
  const WidgetSchema& schema = schema_of(*w);
  if (!r.variant.empty()) {
    t.variant = find_variant(schema, r.variant);
    if (!t.variant) bad_rule(r, "names no variant of it");
  }
  if (r.state.empty()) {
    t.state = StyleState::rest;
  } else {
    const std::optional<StyleState> s = find_style_state(r.state);
    if (!s || !(schema.states & state_bit(*s))) bad_rule(r, "names no state it has");
    t.state = *s;
  }
  const std::optional<StyleProperty> p = find_property(schema, t.state, r.property);
  if (!p) bad_rule(r, "names no property it has in that state");
  t.property = *p;
  return t;
}

void apply(const StyleRule& r, const RuleTarget& t, WidgetStyle& s) {
  switch (t.property.kind) {
    case StyleProperty::Kind::color:
      set(s, t.property.slot, t.state, r.color);
      return;
    case StyleProperty::Kind::radius:
      s.radius = r.number;
      return;
    case StyleProperty::Kind::opacity:
      s.disabled_opacity = r.number;
      return;
  }
}

}  // namespace

std::span<const WidgetSchema> widget_schemas() { return kSchemas; }

const WidgetSchema& schema_of(StyledWidget w) { return kSchemas[static_cast<size_t>(w)]; }

size_t style_index(StyledWidget w, size_t variant) {
  size_t i = 0;
  for (size_t k = 0; k < static_cast<size_t>(w); ++k) i += variant_count(kSchemas[k]);
  return i + variant;
}

std::optional<StyledWidget> find_styled_widget(std::string_view name) {
  for (size_t i = 0; i < kStyledWidgetCount; ++i) {
    if (kSchemas[i].name == name) return static_cast<StyledWidget>(i);
  }
  return std::nullopt;
}

std::optional<size_t> find_variant(const WidgetSchema& schema, std::string_view name) {
  for (size_t i = 0; i < schema.variants.size(); ++i) {
    if (schema.variants[i] == name) return i;
  }
  return std::nullopt;
}

std::optional<StyleState> find_style_state(std::string_view name) {
  for (size_t i = 1; i < kStyleStateCount; ++i) {
    if (kStyleStateNames[i] == name) return static_cast<StyleState>(i);
  }
  return std::nullopt;
}

std::optional<StyleProperty> find_property(const WidgetSchema& schema, StyleState state,
                                           std::string_view name) {
  for (size_t i = 0; i < schema.colors.size(); ++i) {
    if (schema.colors[i] == name) return StyleProperty{StyleProperty::Kind::color, i};
  }
  if (name == "radius" && schema.radius && state == StyleState::rest) {
    return StyleProperty{StyleProperty::Kind::radius, 0};
  }
  if (name == "opacity" && state == StyleState::disabled) {
    return StyleProperty{StyleProperty::Kind::opacity, 0};
  }
  return std::nullopt;
}

Color WidgetStyle::color(size_t slot, const StyleMix& mix) const {
  const auto& by_state = colors[slot].by_state;
  Color c = *by_state[state_index(StyleState::rest)];
  const auto toward = [&](StyleState s, float t) {
    if (const std::optional<Color>& to = by_state[state_index(s)]) c = blend_colors(c, *to, t);
  };
  toward(StyleState::hover, mix.hover);
  toward(StyleState::pressed, mix.pressed);
  toward(StyleState::checked, mix.checked);
  if (const std::optional<Color>& f = by_state[state_index(StyleState::focused)]; f && mix.focused)
    c = *f;
  if (mix.disabled) {
    const std::optional<Color>& d = by_state[state_index(StyleState::disabled)];
    c = d ? *d : c.with_alpha(c.a * disabled_opacity);
  }
  return c;
}

WidgetStyles compute_widget_styles(const Theme& theme) {
  WidgetStyles out;
  std::vector<RuleTarget> targets;
  targets.reserve(theme.widget_styles.size());
  for (const StyleRule& r : theme.widget_styles) targets.push_back(locate(r));

  for (size_t stage = 0; stage < kStyleStateCount; ++stage) {
    const StyleState state = static_cast<StyleState>(stage);
    for (size_t w = 0; w < kStyledWidgetCount; ++w) {
      const StyledWidget widget = static_cast<StyledWidget>(w);
      for (size_t v = 0; v < variant_count(kSchemas[w]); ++v) {
        defaults(state, widget, v, theme, out.all[style_index(widget, v)]);
      }
    }
    // Rules for every variant first, so a rule for one variant wins.
    for (const bool one_variant : {false, true}) {
      for (size_t i = 0; i < targets.size(); ++i) {
        const RuleTarget& t = targets[i];
        if (t.state != state || t.variant.has_value() != one_variant) continue;
        const size_t first = t.variant ? *t.variant : 0;
        const size_t last = t.variant ? *t.variant + 1 : variant_count(schema_of(t.widget));
        for (size_t v = first; v < last; ++v) {
          apply(theme.widget_styles[i], t, out.all[style_index(t.widget, v)]);
        }
      }
    }
  }
  return out;
}

Color blend_colors(Color from, Color to, float t) {
  if (t <= 0) return from;
  if (from.a == to.a) return Color::mix(from, to, t);
  if (t >= 1) return to;
  const float wf = from.a * (1 - t), wt = to.a * t;
  const float a = wf + wt;
  if (a <= 0) return to;
  return {(from.r * wf + to.r * wt) / a, (from.g * wf + to.g * wt) / a,
          (from.b * wf + to.b * wt) / a, a};
}

Color over(Color under, Color top) {
  const float wu = under.a * (1 - top.a);
  const float a = top.a + wu;
  if (a <= 0) return kTransparent;
  return {(top.r * top.a + under.r * wu) / a, (top.g * top.a + under.g * wu) / a,
          (top.b * top.a + under.b * wu) / a, a};
}

}  // namespace smalti::detail
