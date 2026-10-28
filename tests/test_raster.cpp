// The rasterizer and canvas against shapes whose coverage is known exactly.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numeric>

#include "check.hpp"
#include "render/canvas.hpp"
#include "render/raster.hpp"

using namespace smalti;
using namespace smalti::detail;

namespace {

constexpr float kPi = 3.14159265358979f;
const RectI kBig{-1000, -1000, 4000, 4000};

double area(const MaskView& m) {
  return std::accumulate(m.coverage,
                         m.coverage + static_cast<size_t>(m.w) * static_cast<size_t>(m.h), 0.0) /
         255.0;
}

Path rect(float x, float y, float w, float h) {
  Path p;
  p.rounded_rect({x, y, w, h}, 0);
  return p;
}

}  // namespace

TEST(raster_fractional_rect_has_exact_area_and_partial_edges) {
  Rasterizer r;
  const MaskView m = r.fill(rect(0.25f, 0.5f, 3.5f, 2.0f), {}, kBig);
  CHECK_NEAR(area(m), 7.0, 0.02);
  CHECK_NEAR(m.coverage[0] / 255.0, 0.75 * 0.5, 0.01);  // top-left corner pixel
  CHECK_NEAR(m.coverage[static_cast<size_t>(m.w) + 1] / 255.0, 1.0, 0.004);  // interior
}

TEST(raster_triangle_and_circle_areas) {
  Rasterizer r;
  Path tri;
  tri.move_to({0, 0});
  tri.line_to({10, 0});
  tri.line_to({0, 10});
  CHECK_NEAR(area(r.fill(tri, {}, kBig)), 50.0, 0.05);
  // Chords sit inside a curve by at most the flattening tolerance (0.05 px), so a circle can
  // lose at most perimeter * 0.05 of its area.
  Path circle;
  circle.rounded_rect({0, 0, 40, 40}, 20);
  CHECK_NEAR(area(r.fill(circle, {}, kBig)), kPi * 400.0, 2 * kPi * 20 * 0.05);
}

TEST(raster_opposite_winding_cuts_a_hole) {
  Rasterizer r;
  Path ring = rect(0, 0, 10, 10);
  ring.rounded_rect({2, 2, 6, 6}, 0, false);
  CHECK_NEAR(area(r.fill(ring, {}, kBig)), 64.0, 0.05);
}

TEST(raster_clipping_left_right_and_diagonal) {
  Rasterizer r;
  MaskView m = r.fill(rect(0, 0, 10, 10), {}, {6, 0, 100, 100});
  CHECK_NEAR(area(m), 40.0, 0.05);
  CHECK_EQ(m.w, 4);
  m = r.fill(rect(0, 0, 10, 10), {}, {0, 0, 3, 100});
  CHECK_NEAR(area(m), 30.0, 0.05);
  Path wedge;
  wedge.move_to({-5, 0});
  wedge.line_to({5, 10});
  wedge.line_to({5, 0});
  CHECK_NEAR(area(r.fill(wedge, {}, {0, 0, 100, 100})), 37.5, 0.05);
  CHECK(r.fill(rect(0, 0, 10, 10), {}, {50, 50, 10, 10}).empty());  // entirely outside
}

TEST(raster_transform_scales_and_flips) {
  Rasterizer r;
  CHECK_NEAR(area(r.fill(rect(0, 0, 3, 3), {2.0f, -2.0f, 10.0f, 20.0f}, kBig)), 36.0, 0.05);
}

TEST(canvas_shadow_fast_path_matches_direct_integral) {
  // A box nudged off whole pixels by a thousandth takes the direct path; the two images may
  // differ by at most a rounding step.
  Bitmap a(280, 220), b(280, 220);
  Canvas ca, cb;
  ca.set_target(a.view());
  cb.set_target(b.view());
  ca.clear(Color::hex(0xFFFFFF));
  cb.clear(Color::hex(0xFFFFFF));
  ca.shadow({40, 40, 160, 90}, 12, 8, {0, 0, 0, 0.6f}, false);
  cb.shadow({40.001f, 40.001f, 160, 90}, 12, 8, {0, 0, 0, 0.6f}, false);
  int worst = 0;
  for (size_t i = 0; i < a.pixels.size(); ++i) {
    for (int shift : {0, 8, 16}) {
      worst = std::max(worst, std::abs(static_cast<int>(a.pixels[i] >> shift & 0xFF) -
                                       static_cast<int>(b.pixels[i] >> shift & 0xFF)));
    }
  }
  CHECK(worst <= 1);
}

TEST(canvas_fill_rect_whole_pixels_is_exact_and_clipped) {
  Bitmap bmp(10, 10);
  Canvas c;
  c.set_target(bmp.view());
  c.clear(Color::hex(0x000000));
  c.set_clip({0, 0, 5, 10});
  c.fill_rect({2, 2, 6, 6}, Color::hex(0xFF0000));
  CHECK_EQ(bmp.pixels[2 * 10 + 2], 0xFFFF0000u);
  CHECK_EQ(bmp.pixels[2 * 10 + 4], 0xFFFF0000u);
  CHECK_EQ(bmp.pixels[2 * 10 + 5], 0xFF000000u);  // beyond the clip
  CHECK_EQ(bmp.pixels[1 * 10 + 2], 0xFF000000u);  // above the rectangle
}

TEST(canvas_dithered_gradient_averages_to_the_ramp) {
  // A ramp too shallow for 8 bits: dithering must keep each row's average on the true value
  // instead of collapsing to one level.
  Bitmap bmp(256, 64);
  Canvas c;
  c.set_target(bmp.view());
  c.clear(Color::hex(0x000000));
  c.rounded_gradient({0, 0, 256, 64}, 0, Color::hex(0x101010), Color::hex(0x141414), true);
  // Every row: without dithering, rows whose true value sits near a half step miss by about 0.5.
  double worst = 0;
  for (int row = 0; row < 64; ++row) {
    double sum = 0;
    for (int x = 0; x < 256; ++x)
      sum += bmp.pixels[static_cast<size_t>(row) * 256 + static_cast<size_t>(x)] & 0xFF;
    const double expected = 0x10 + (0x14 - 0x10) * ((row + 0.5) / 64.0);
    worst = std::max(worst, std::fabs(sum / 256.0 - expected));
  }
  CHECK(worst < 0.2);
}
