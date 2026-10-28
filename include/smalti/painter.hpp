// Painter: what a widget draws with, in logical pixels. Edges of rectangles are snapped to
// device pixels so hairlines stay crisp at any scale.
#pragma once

#include <span>
#include <string_view>

#include "smalti/color.hpp"
#include "smalti/geometry.hpp"

namespace smalti {

enum class FontWeight : uint8_t { regular, medium, semibold };

struct TextStyle {
  float size = 14;
  FontWeight weight = FontWeight::regular;
};

namespace detail {
struct PaintState;
}

class Painter {
 public:
  explicit Painter(detail::PaintState& state) : s_(state) {}

  void fill_rect(Rect r, Color c);
  void fill_rounded_rect(Rect r, float radius, Color c);
  // A border of `width` drawn inside `r`. A width of 0 means one device pixel.
  void stroke_rounded_rect(Rect r, float radius, float width, Color c);
  void fill_circle(Point center, float radius, Color c);
  void stroke_circle(Point center, float radius, float width, Color c);
  // Round-jointed lines, for icons and check marks.
  void polyline(std::span<const Point> points, float width, Color c);
  // The soft shadow of a rounded rectangle; `surface` is the rectangle that will be drawn on
  // top (its interior is skipped).
  void shadow(Rect box, float radius, float blur, Color c, Rect surface = {});
  void vertical_gradient(Rect r, float radius, Color top, Color bottom);

  // Draws one line of text with its baseline at `baseline.y`; returns the x where it ended.
  float text(Point baseline, std::string_view utf8, const TextStyle& style, Color c);
  float text_width(std::string_view utf8, const TextStyle& style) const;
  float ascent(const TextStyle& style) const;   // above the baseline, positive
  float descent(const TextStyle& style) const;  // below the baseline, positive
  float line_height(const TextStyle& style) const;

  float hairline() const;  // one device pixel in logical pixels
  float scale() const;     // device pixels per logical pixel

  void push_clip(Rect r);
  void pop_clip();

 private:
  detail::PaintState& s_;
};

}  // namespace smalti
