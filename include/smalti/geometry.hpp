// Points, sizes, rectangles and insets in logical pixels (1/96 inch). Device pixels never
// appear in the public API.
#pragma once

#include <algorithm>

namespace smalti {

struct Point {
  float x = 0;
  float y = 0;
  friend bool operator==(Point, Point) = default;
};

struct Size {
  float width = 0;
  float height = 0;
  friend bool operator==(Size, Size) = default;
};

struct Insets {
  float left = 0, top = 0, right = 0, bottom = 0;
  static constexpr Insets all(float v) { return {v, v, v, v}; }
  static constexpr Insets symmetric(float horizontal, float vertical) {
    return {horizontal, vertical, horizontal, vertical};
  }
  constexpr float horizontal() const { return left + right; }
  constexpr float vertical() const { return top + bottom; }
  friend bool operator==(Insets, Insets) = default;
};

struct Rect {
  float x = 0, y = 0, width = 0, height = 0;

  constexpr float right() const { return x + width; }
  constexpr float bottom() const { return y + height; }
  constexpr Point center() const { return {x + width * 0.5f, y + height * 0.5f}; }
  constexpr Size size() const { return {width, height}; }
  constexpr bool empty() const { return width <= 0 || height <= 0; }
  constexpr bool contains(Point p) const {
    return p.x >= x && p.y >= y && p.x < right() && p.y < bottom();
  }
  constexpr Rect inset(Insets i) const {
    return {x + i.left, y + i.top, std::max(0.0f, width - i.horizontal()),
            std::max(0.0f, height - i.vertical())};
  }
  constexpr Rect intersect(const Rect& o) const {
    const float x0 = std::max(x, o.x), y0 = std::max(y, o.y);
    const float x1 = std::min(right(), o.right()), y1 = std::min(bottom(), o.bottom());
    return {x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
  }
  constexpr bool intersects(const Rect& o) const { return !intersect(o).empty(); }
  friend bool operator==(const Rect&, const Rect&) = default;
};

}  // namespace smalti
