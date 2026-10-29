// Anti-aliased scanline rasterizer: exact-area coverage from signed-area accumulation (the
// method of font-rs and stb_truetype 2). One rasterizer fills glyphs and every UI shape, so
// their edges are anti-aliased alike.
#pragma once

#include <cstdint>
#include <vector>

#include "render/geometry.hpp"

namespace smalti::detail {

// Coverage produced by the rasterizer: one byte per pixel, 0 outside, 255 inside.
struct MaskView {
  int x = 0, y = 0;  // device position of the top-left pixel
  int w = 0, h = 0;
  const uint8_t* coverage = nullptr;
  bool empty() const { return w <= 0 || h <= 0; }
};

class Rasterizer {
 public:
  // Fills `path` (after `t`, in device pixels) with the nonzero rule, clipped to `clip`. The
  // view stays valid until the next call; the storage is reused, so steady drawing does not
  // allocate.
  MaskView fill(const Path& path, const Transform& t, RectI clip);

 private:
  void reset(RectI area);
  void line(Point p0, Point p1);
  void accumulate_line(Point p0, Point p1);
  void quad(Point p0, Point c, Point p1);
  void cubic(Point p0, Point c1, Point c2, Point p1);

  RectI area_;
  int stride_ = 0;
  std::vector<float> acc_;  // signed area per cell; a running sum gives coverage
  std::vector<uint8_t> coverage_;
};

}  // namespace smalti::detail
