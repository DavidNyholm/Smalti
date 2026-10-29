// Widget styles: what a theme's "widgets" section may say about each built-in widget (the
// schema), and the styles widgets draw with: the built-in look computed from the tokens, with
// the theme's rules applied over it.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "smalti/theme.hpp"

namespace smalti::detail {

// The states a style can set, in the order they apply: each moves the colour from what the
// states before it gave towards its own. Rest is the colour with no state.
enum class StyleState : uint8_t { rest, hover, pressed, checked, focused, disabled };
inline constexpr size_t kStyleStateCount = 6;
inline constexpr std::array<std::string_view, kStyleStateCount> kStyleStateNames = {
    "", "hover", "pressed", "checked", "focused", "disabled"};

constexpr uint8_t state_bit(StyleState s) {
  return static_cast<uint8_t>(1u << static_cast<int>(s));
}

enum class StyledWidget : uint8_t {
  button,
  check_box,
  switch_toggle,
  radio_button,
  slider,
  progress_bar,
  separator,
  label,
  card,
};
inline constexpr size_t kStyledWidgetCount = 9;

// Each widget's colour properties, in the order of its schema's names below.
struct ButtonSlot {
  enum : uint8_t { fill, border, text, focus };
};
struct CheckBoxSlot {
  enum : uint8_t { fill, border, mark, text, focus };
};
struct SwitchSlot {
  enum : uint8_t { track, border, knob, text, focus };
};
struct RadioSlot {
  enum : uint8_t { fill, border, mark, text, focus };
};
struct SliderSlot {
  enum : uint8_t { track, fill, knob, border, focus };
};
struct ProgressSlot {
  enum : uint8_t { track, fill };
};
struct SeparatorSlot {
  enum : uint8_t { fill };
};
struct LabelSlot {
  enum : uint8_t { text };
};
struct CardSlot {
  enum : uint8_t { fill, border };
};
inline constexpr size_t kMaxStyleColors = 5;

// What a theme file may set for one widget.
struct WidgetSchema {
  std::string_view name;                       // "Button"
  std::span<const std::string_view> variants;  // "primary", ...; in the widget's enum order
  std::span<const std::string_view> colors;    // property names, in slot order
  bool radius = false;                         // takes "radius" (at rest only)
  uint8_t states = 0;                          // state_bit() of each state it has
};

// A property a rule sets: one of the widget's colours, its radius, or its disabled opacity.
struct StyleProperty {
  enum class Kind : uint8_t { color, radius, opacity };
  Kind kind = Kind::color;
  size_t slot = 0;  // which colour
};

// Indexed by StyledWidget.
std::span<const WidgetSchema> widget_schemas();
std::optional<StyledWidget> find_styled_widget(std::string_view name);
std::optional<size_t> find_variant(const WidgetSchema& schema, std::string_view name);
std::optional<StyleState> find_style_state(std::string_view name);  // never rest
// Nothing when the widget has no such property in that state: "radius" is set at rest only,
// and "opacity" under disabled only.
std::optional<StyleProperty> find_property(const WidgetSchema& schema, StyleState state,
                                           std::string_view name);
const WidgetSchema& schema_of(StyledWidget w);
// The widget's first entry in WidgetStyles::all; its variants follow it.
size_t style_index(StyledWidget w, size_t variant = 0);
inline constexpr size_t kStyledVariantCount = 16;

// A disabled control draws at this opacity unless its style says otherwise.
inline constexpr float kDisabledOpacity = 0.45f;

// One colour of a widget across its states: the colour at rest, and for each state the colour
// it moves to, or nothing when that state leaves the colour alone.
struct StateColor {
  std::array<std::optional<Color>, kStyleStateCount> by_state;  // [rest] is always set
};

// How far each state has come, from the widget's own animations.
struct StyleMix {
  float hover = 0, pressed = 0, checked = 0;
  bool focused = false;
  bool disabled = false;
};

struct WidgetStyle {
  std::array<StateColor, kMaxStyleColors> colors;
  float radius = 0;
  float disabled_opacity = kDisabledOpacity;

  // The colour of `slot` for the widget's current states.
  Color color(size_t slot, const StyleMix& mix) const;
};

struct WidgetStyles {
  std::array<WidgetStyle, kStyledVariantCount> all;
  const WidgetStyle& get(StyledWidget w, size_t variant = 0) const {
    return all[style_index(w, variant)];
  }
};

// The built-in look from the theme's tokens, then the theme's rules over it. A rule that names
// no real widget, variant, state or property is a contract violation (the loader never makes
// one; a theme built in code can).
WidgetStyles compute_widget_styles(const Theme& theme);

// Moves colour `from` towards `to` by t in 0..1. Colours of equal opacity mix like Color::mix;
// otherwise the mix is premultiplied, as CSS transitions are, so a fade to transparent keeps
// its hue instead of passing through the transparent colour's.
Color blend_colors(Color from, Color to, float t);
// `top` drawn over `under`, as one colour (both straight alpha, encoded sRGB, as shapes blend).
Color over(Color under, Color top);

}  // namespace smalti::detail
