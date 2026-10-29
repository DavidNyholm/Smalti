// A theme: the colours, sizes and motion every built-in widget draws with. Themes are files in
// JSON with comments (docs/Themes.md describes the format); the three built-in ones are such
// files compiled into the library.
#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

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
  friend bool operator==(const TextTuning&, const TextTuning&) = default;
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
  friend bool operator==(const Theme&, const Theme&) = default;
};

const Theme& theme_dark();   // very dark grey: the default dark theme, kind to LCDs
const Theme& theme_black();  // true black: pixels off on OLED, depth from borders
const Theme& theme_light();

// A problem in a theme file. Line and column are 1-based, and the column counts characters, as
// editors do; both are 0 when the problem is the whole file, such as one that cannot be read.
struct ThemeError {
  int line = 0;
  int column = 0;
  std::string message;
};

// What reading a theme file gives. A theme is never half-applied: when there are errors,
// `theme` is the built-in theme the file names as its base, unchanged (Dark when the file names
// none, or cannot be read that far).
struct ThemeLoad {
  Theme theme;
  std::vector<ThemeError> errors;  // in file order; every problem found, not just the first
  bool ok() const { return errors.empty(); }
};

// Reads a theme from its text. `source` is the file name, used as the theme's name when the
// file sets none ("night.jsonc" names it "night").
ThemeLoad parse_theme(std::string_view text, std::string_view source = {});
// Reads a theme file (at most 1 MB). A file that cannot be read is an error at line 0.
ThemeLoad load_theme(const std::filesystem::path& path);

}  // namespace smalti
