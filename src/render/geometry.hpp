// Device-pixel geometry and paths used inside the renderer.
#pragma once

#include <cstdint>
#include <vector>

#include "smalti/geometry.hpp"

namespace smalti::detail {

// A rectangle in device pixels with fractional edges.
struct RectF {
  float x = 0, y = 0, w = 0, h = 0;
  float right() const { return x + w; }
  float bottom() const { return y + h; }
};

// A rectangle of whole device pixels.
struct RectI {
  int x = 0, y = 0, w = 0, h = 0;
  int right() const { return x + w; }
  int bottom() const { return y + h; }
  bool empty() const { return w <= 0 || h <= 0; }
  RectI intersect(const RectI& o) const;
  RectI unite(const RectI& o) const;  // the bounding box of both; empty rects are ignored
  friend bool operator==(const RectI&, const RectI&) = default;
};

// x' = sx * x + tx, y' = sy * y + ty. Glyphs need no more than scale, flip and offset.
struct Transform {
  float sx = 1, sy = 1, tx = 0, ty = 0;
  Point apply(Point p) const { return {sx * p.x + tx, sy * p.y + ty}; }
};

// Move, line, quadratic and cubic segments. Every contour is closed when filled.
class Path {
 public:
  enum class Verb : uint8_t { kMove, kLine, kQuad, kCubic };

  void clear();  // keeps the storage, so a reused path does not allocate
  void move_to(Point p);
  void line_to(Point p);
  void quad_to(Point c, Point p);
  void cubic_to(Point c1, Point c2, Point p);
  // One contour; `clockwise` false winds the other way, which cuts a hole in a shape drawn
  // clockwise (a border is an outer rectangle minus an inner one).
  void rounded_rect(RectF r, float radius, bool clockwise = true);

  bool empty() const { return verbs_.empty(); }
  const std::vector<Verb>& verbs() const { return verbs_; }
  const std::vector<Point>& points() const { return points_; }

 private:
  std::vector<Verb> verbs_;
  std::vector<Point> points_;
};

}  // namespace smalti::detail
