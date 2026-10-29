#include "render/raster.hpp"

#include <algorithm>
#include <cmath>

namespace smalti::detail {

namespace {
// Edges flatter than this are skipped: their area is far below anything visible.
constexpr float kMinEdgeHeight = 1e-6f;

// Maximum distance, in device pixels, between a curve and the segments that replace it.
// Chords lie inside the curve, so round shapes lose up to this much along their edge; at a
// twentieth of a pixel, round letters keep the same weight as straight ones.
constexpr float kFlattenTolerance = 0.05f;
// A curve is never split into more segments than this, whatever its size.
constexpr int kMaxCurveSegments = 256;
// Each row keeps two spare cells: an edge clamped to the right border writes one past it.
constexpr int kRowSlack = 2;

Point lerp(Point a, Point b, float t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}; }

// Segments for a curve from Wang's estimate, clamped while still floating point: points far out
// make the estimate infinite (or NaN), which no int holds.
int curve_segments(double estimate, int most) {
  if (!(estimate >= 1.0)) return 1;
  return static_cast<int>(std::min(std::ceil(estimate), static_cast<double>(most)));
}

}  // namespace

MaskView Rasterizer::fill(const Path& path, const Transform& t, RectI clip) {
  if (path.empty()) return {};
  // The control points' hull contains the curve, so their bounds contain the shape.
  float x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
  for (Point p : path.points()) {
    const Point d = t.apply(p);
    x0 = std::min(x0, d.x);
    y0 = std::min(y0, d.y);
    x1 = std::max(x1, d.x);
    y1 = std::max(y1, d.y);
  }
  const RectI area =
      rect_from_edges(std::floor(x0), std::floor(y0), std::ceil(x1), std::ceil(y1)).intersect(clip);
  if (area.empty()) return {};
  reset(area);

  const auto& pts = path.points();
  size_t i = 0;
  Point start{}, cur{};
  bool open = false;
  for (Path::Verb verb : path.verbs()) {
    switch (verb) {
      case Path::Verb::kMove:
        if (open) line(cur, start);  // every contour is closed for filling
        start = cur = t.apply(pts[i++]);
        open = true;
        break;
      case Path::Verb::kLine: {
        const Point p = t.apply(pts[i++]);
        line(cur, p);
        cur = p;
        break;
      }
      case Path::Verb::kQuad: {
        const Point c = t.apply(pts[i]), p = t.apply(pts[i + 1]);
        i += 2;
        quad(cur, c, p);
        cur = p;
        break;
      }
      case Path::Verb::kCubic: {
        const Point c1 = t.apply(pts[i]), c2 = t.apply(pts[i + 1]), p = t.apply(pts[i + 2]);
        i += 3;
        cubic(cur, c1, c2, p);
        cur = p;
        break;
      }
    }
  }
  if (open) line(cur, start);

  coverage_.resize(static_cast<size_t>(area.w) * static_cast<size_t>(area.h));
  for (int row = 0; row < area.h; ++row) {
    const float* cells = &acc_[static_cast<size_t>(row) * static_cast<size_t>(stride_)];
    uint8_t* out = &coverage_[static_cast<size_t>(row) * static_cast<size_t>(area.w)];
    float sum = 0;
    for (int x = 0; x < area.w; ++x) {
      sum += cells[x];
      // abs() makes both windings fill; min() approximates nonzero where contours overlap.
      out[x] = static_cast<uint8_t>(std::min(std::fabs(sum), 1.0f) * 255.0f + 0.5f);
    }
  }
  return {area.x, area.y, area.w, area.h, coverage_.data()};
}

void Rasterizer::reset(RectI area) {
  area_ = area;
  stride_ = area.w + kRowSlack;
  acc_.assign(static_cast<size_t>(stride_) * static_cast<size_t>(area.h), 0.0f);
}

void Rasterizer::line(Point a, Point b) {
  a.x -= static_cast<float>(area_.x);
  a.y -= static_cast<float>(area_.y);
  b.x -= static_cast<float>(area_.x);
  b.y -= static_cast<float>(area_.y);
  if (a.y == b.y) return;
  // Split where the segment crosses the left and right borders. A piece outside is moved onto
  // the border it is beyond: on the left it still covers everything to its right; on the right
  // it lands in the spare cells and touches no visible pixel.
  const float w = static_cast<float>(area_.w);
  float cuts[4] = {0.0f, 1.0f, 1.0f, 1.0f};
  int n = 1;
  for (float edge : {0.0f, w}) {
    if ((a.x < edge) != (b.x < edge)) cuts[n++] = (edge - a.x) / (b.x - a.x);
  }
  if (n == 3 && cuts[1] > cuts[2]) std::swap(cuts[1], cuts[2]);  // at most two cuts to order
  cuts[n++] = 1.0f;
  for (int k = 0; k + 1 < n; ++k) {
    Point p0 = lerp(a, b, cuts[k]), p1 = lerp(a, b, cuts[k + 1]);
    p0.x = std::clamp(p0.x, 0.0f, w);
    p1.x = std::clamp(p1.x, 0.0f, w);
    accumulate_line(p0, p1);
  }
}

void Rasterizer::accumulate_line(Point p0, Point p1) {
  // An edge this flat covers no measurable area, and its slope (dx / dy) would overflow; a
  // non-finite point cannot be placed at all. Written so that NaN is skipped too.
  if (!(std::fabs(p1.y - p0.y) >= kMinEdgeHeight)) return;
  if (!std::isfinite(p0.x) || !std::isfinite(p1.x)) return;
  float dir = 1.0f;
  if (p0.y > p1.y) {
    std::swap(p0, p1);
    dir = -1.0f;
  }
  const float dxdy = (p1.x - p0.x) / (p1.y - p0.y);
  const float w = static_cast<float>(area_.w);
  const float y_start = std::max(p0.y, 0.0f);
  if (y_start >= static_cast<float>(area_.h) || p1.y <= 0.0f) return;
  float x = p0.x + (y_start - p0.y) * dxdy;
  const int row_end = static_cast<int>(std::min<double>(area_.h, std::ceil(double{p1.y})));
  for (int row = static_cast<int>(y_start); row < row_end; ++row) {
    float* cells = &acc_[static_cast<size_t>(row) * static_cast<size_t>(stride_)];
    const float dy =
        std::min(static_cast<float>(row + 1), p1.y) - std::max(static_cast<float>(row), p0.y);
    const float x_next = x + dxdy * dy;
    const float d = dy * dir;
    // Rounding can push x a hair outside [0, w]; the cell index must not follow it.
    const float xa = std::clamp(std::min(x, x_next), 0.0f, w);
    const float xb = std::clamp(std::max(x, x_next), 0.0f, w);
    const float xa_floor = std::floor(xa);
    const int ia = static_cast<int>(xa_floor);
    const float xb_ceil = std::ceil(xb);
    const int ib = static_cast<int>(xb_ceil);
    if (ib <= ia + 1) {
      // The edge stays within one cell: split its area between that cell and the next.
      const float x_mid = 0.5f * (xa + xb) - xa_floor;
      cells[ia] += d - d * x_mid;
      cells[ia + 1] += d * x_mid;
    } else {
      // The edge crosses several cells: a triangle in the first and last, equal steps between.
      const float s = 1.0f / (xb - xa);
      const float xa_frac = xa - xa_floor;
      const float a0 = 0.5f * s * (1.0f - xa_frac) * (1.0f - xa_frac);
      const float xb_frac = xb - xb_ceil + 1.0f;
      const float am = 0.5f * s * xb_frac * xb_frac;
      cells[ia] += d * a0;
      if (ib == ia + 2) {
        cells[ia + 1] += d * (1.0f - a0 - am);
      } else {
        const float a1 = s * (1.5f - xa_frac);
        cells[ia + 1] += d * (a1 - a0);
        for (int xi = ia + 2; xi < ib - 1; ++xi) cells[xi] += d * s;
        const float a2 = a1 + static_cast<float>(ib - ia - 3) * s;
        cells[ib - 1] += d * (1.0f - a2 - am);
      }
      cells[ib] += d * am;
    }
    x = x_next;
  }
}

void Rasterizer::quad(Point p0, Point c, Point p1) {
  // Wang's formula: segments needed so no chord strays more than the tolerance from the curve.
  // In double: far-off points square past what a float holds.
  const double dd = std::hypot(double{p0.x} - 2.0 * c.x + p1.x, double{p0.y} - 2.0 * c.y + p1.y);
  const int n = curve_segments(std::sqrt(dd / (4.0 * kFlattenTolerance)), kMaxCurveSegments);
  Point prev = p0;
  for (int k = 1; k <= n; ++k) {
    const float t = static_cast<float>(k) / static_cast<float>(n), u = 1.0f - t;
    const Point p{u * u * p0.x + 2 * u * t * c.x + t * t * p1.x,
                  u * u * p0.y + 2 * u * t * c.y + t * t * p1.y};
    line(prev, p);
    prev = p;
  }
}

void Rasterizer::cubic(Point p0, Point c1, Point c2, Point p1) {
  const double dd =
      std::max(std::hypot(double{p0.x} - 2.0 * c1.x + c2.x, double{p0.y} - 2.0 * c1.y + c2.y),
               std::hypot(double{c1.x} - 2.0 * c2.x + p1.x, double{c1.y} - 2.0 * c2.y + p1.y));
  const int n = curve_segments(std::sqrt(0.75 * dd / kFlattenTolerance), kMaxCurveSegments);
  Point prev = p0;
  for (int k = 1; k <= n; ++k) {
    const float t = static_cast<float>(k) / static_cast<float>(n), u = 1.0f - t;
    const float b0 = u * u * u, b1 = 3 * u * u * t, b2 = 3 * u * t * t, b3 = t * t * t;
    const Point p{b0 * p0.x + b1 * c1.x + b2 * c2.x + b3 * p1.x,
                  b0 * p0.y + b1 * c1.y + b2 * c2.y + b3 * p1.y};
    line(prev, p);
    prev = p;
  }
}

}  // namespace smalti::detail
