#include "render/geometry.hpp"

#include <algorithm>

namespace smalti::detail {

namespace {
// Cubic Bezier control-point distance that approximates a quarter circle of radius 1.
constexpr float kCircleKappa = 0.5522847498f;
}  // namespace

RectI RectI::intersect(const RectI& o) const {
  const int x0 = std::max(x, o.x), y0 = std::max(y, o.y);
  const int x1 = std::min(right(), o.right()), y1 = std::min(bottom(), o.bottom());
  return {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

RectI RectI::unite(const RectI& o) const {
  if (empty()) return o;
  if (o.empty()) return *this;
  const int x0 = std::min(x, o.x), y0 = std::min(y, o.y);
  return {x0, y0, std::max(right(), o.right()) - x0, std::max(bottom(), o.bottom()) - y0};
}

void Path::clear() {
  verbs_.clear();
  points_.clear();
}

void Path::move_to(Point p) {
  verbs_.push_back(Verb::kMove);
  points_.push_back(p);
}

void Path::line_to(Point p) {
  verbs_.push_back(Verb::kLine);
  points_.push_back(p);
}

void Path::quad_to(Point c, Point p) {
  verbs_.push_back(Verb::kQuad);
  points_.push_back(c);
  points_.push_back(p);
}

void Path::cubic_to(Point c1, Point c2, Point p) {
  verbs_.push_back(Verb::kCubic);
  points_.push_back(c1);
  points_.push_back(c2);
  points_.push_back(p);
}

void Path::rounded_rect(RectF r, float radius, bool clockwise) {
  radius = std::clamp(radius, 0.0f, std::min(r.w, r.h) * 0.5f);
  const float k = radius * kCircleKappa;
  const float x0 = r.x, y0 = r.y, x1 = r.right(), y1 = r.bottom();
  // Corner points clockwise on screen (y down), starting after the top-left arc.
  const Point pts[] = {
      {x0 + radius, y0}, {x1 - radius, y0},  // top edge
      {x1, y0 + radius}, {x1, y1 - radius},  // right edge
      {x1 - radius, y1}, {x0 + radius, y1},  // bottom edge
      {x0, y1 - radius}, {x0, y0 + radius},  // left edge
  };
  // Control points for the four arcs, in the order of the edges they follow.
  const Point ctl[][2] = {
      {{x1 - radius + k, y0}, {x1, y0 + radius - k}},  // top-right
      {{x1, y1 - radius + k}, {x1 - radius + k, y1}},  // bottom-right
      {{x0 + radius - k, y1}, {x0, y1 - radius + k}},  // bottom-left
      {{x0, y0 + radius - k}, {x0 + radius - k, y0}},  // top-left
  };
  move_to(pts[0]);
  if (clockwise) {
    for (int side = 0; side < 4; ++side) {
      line_to(pts[side * 2 + 1]);
      cubic_to(ctl[side][0], ctl[side][1], pts[(side * 2 + 2) % 8]);
    }
  } else {
    for (int side = 3; side >= 0; --side) {
      cubic_to(ctl[side][1], ctl[side][0], pts[side * 2 + 1]);
      line_to(pts[side * 2]);
    }
  }
}

}  // namespace smalti::detail
