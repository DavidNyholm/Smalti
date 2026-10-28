// A theme: the colours, sizes and motion every built-in widget draws with. v0.1 ships three
// built-in themes defined in code; theme files that can be edited live arrive in v0.2.
#pragma once

#include <chrono>
#include <string>

#include "smalti/color.hpp"

namespace smalti {

// How glyph coverage becomes colour. The choice changes how heavy text looks.
enum class TextBlend : uint8_t {
  srgb,      // blend encoded values: dark text heavier, light text thinner
  linear,    // blend in linear light: light text heavier, dark text thinner
  adaptive,  // linear for light text, sRGB for dark, so both read at a similar weight
};

struct TextTuning {
  TextBlend blend = TextBlend::adaptive;
  float contrast = 0.1f;      // > 0 sharpens edges: coverage c becomes c * (1 + k) / (1 + k * c)
  bool snap_x_height = true;  // stretch vertically so the x-height lands on a whole pixel
};

struct Theme {
  std::string name;

  // Surfaces, from the window frame inwards.
  Color titlebar, sidebar, background, raised;
  Color border, divider, hover, selected;

  Color text, text_secondary, text_muted;
  Color accent, accent_hover, on_accent, accent_soft;
  Color success, warning, danger;

  // Depth: a soft shadow where it can be seen, a top highlight where it can't (true black).
  bool shadows = true;
  Color shadow;
  Color top_highlight;

  float radius_control = 8;
  float radius_card = 12;
  float control_height = 34;

  float text_caption = 12;
  float text_body = 14;
  float text_title = 18;
  float text_heading = 24;

  std::chrono::milliseconds motion_fast{120};
  std::chrono::milliseconds motion_base{200};

  TextTuning text_tuning;
  bool dark = true;  // asks the platform for a dark window frame
};

const Theme& theme_dark();   // very dark grey: the default dark theme, kind to LCDs
const Theme& theme_black();  // true black: pixels off on OLED, depth from borders
const Theme& theme_light();

}  // namespace smalti
