// A CPU canvas over 32-bit pixels: filled and stroked shapes, analytic soft shadows, dithered
// gradients, and glyph masks blended with the theme's text tuning.
#pragma once

#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

#include "render/geometry.hpp"
#include "render/raster.hpp"
#include "smalti/color.hpp"
#include "smalti/theme.hpp"

namespace smalti::detail {

// Pixels as 0xAARRGGBB words: B, G, R, A in memory, the layout GDI, X11 (on little-endian
// machines) and DXGI's B8G8R8A8 expect. Everything drawn is opaque, so alpha stays 255.
// A view: the memory belongs to whoever made it (a platform buffer, or a Bitmap).
struct PixelBuffer {
  uint32_t* data = nullptr;
  int width = 0, height = 0;
  int stride = 0;  // pixels per row
  uint32_t* row(int y) const { return data + static_cast<size_t>(y) * static_cast<size_t>(stride); }
  bool empty() const { return data == nullptr || width <= 0 || height <= 0; }
};

struct Bitmap {
  int width = 0, height = 0;
  std::vector<uint32_t> pixels;
  Bitmap() = default;
  Bitmap(int w, int h) : width(w), height(h), pixels(static_cast<size_t>(w) * static_cast<size_t>(h), 0xFF000000u) {}
  PixelBuffer view() { return {pixels.data(), width, height, width}; }
};

class Canvas {
 public:
  Canvas() = default;
  void set_target(PixelBuffer target);
  const PixelBuffer& target() const { return target_; }
  void set_clip(RectI clip);  // clamped to the target
  RectI clip() const { return clip_; }

  void clear(Color c);
  void fill_rect(RectF r, Color c);
  void fill_rounded_rect(RectF r, float radius, Color c);
  void stroke_rounded_rect(RectF r, float radius, float width, Color c);  // inside r
  void fill_path(const Path& path, const Transform& t, Color c);
  void stroke_polyline(std::span<const Point> points, float width, Color c);
  // The blurred shadow of a rounded rectangle, computed in closed form. Pixels inside
  // `occluder` are skipped: the opaque surface drawn next covers them anyway.
  void shadow(RectF box, float radius, float sigma, Color c, bool dither, RectF occluder = {});
  void rounded_gradient(RectF r, float radius, Color top, Color bottom, bool dither);
  // Blends a glyph's coverage placed with its top-left pixel at (x, y).
  void blend_glyph(MaskView mask, int x, int y, Color c, const TextTuning& tuning);

  size_t cache_bytes() const;

 private:
  void blend_mask(MaskView mask, Color c);
  const std::vector<float>& shadow_corner(float sigma, float corner, int size);

  PixelBuffer target_;
  RectI clip_;
  Rasterizer raster_;
  Path path_;  // reused by every shape, so drawing does not allocate
  std::vector<float> shadow_cols_, shadow_rows_;
  // One corner of a blurred rounded box, shared by every shadow with the same blur and radius.
  std::unordered_map<uint64_t, std::vector<float>> shadow_corners_;
};

}  // namespace smalti::detail
