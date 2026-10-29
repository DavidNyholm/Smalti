#include "render/canvas.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace smalti::detail {

namespace {

constexpr float kPi = 3.14159265358979f;
// Linear-light values are encoded back to sRGB through a table of this many steps; 4096 is
// finer than 8-bit output can show.
constexpr int kEncodeSteps = 4096;
// A Gaussian is treated as zero beyond this many standard deviations.
constexpr float kShadowReach = 3.0f;
// Samples per pixel when integrating a shadow vertically (Evan Wallace's method uses 4). Only
// corner tiles take this path, and they are cached, so 8 is cheap and keeps them seamless
// with the exact closed-form edges next to them.
constexpr int kShadowSamples = 8;
// Shadow blur and corner radius are snapped to this many steps per pixel: finer than can be
// seen, and the corner-tile cache key.
constexpr float kShadowKeySteps = 64;

float srgb_to_linear(float v) {
  return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}
float linear_to_srgb(float v) {
  return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

struct GammaTables {
  std::array<float, 256> decode{};
  std::array<uint8_t, kEncodeSteps> encode{};
  GammaTables() {
    for (int i = 0; i < 256; ++i)
      decode[static_cast<size_t>(i)] = srgb_to_linear(static_cast<float>(i) / 255.0f);
    for (int i = 0; i < kEncodeSteps; ++i) {
      const float v = linear_to_srgb(static_cast<float>(i) / (kEncodeSteps - 1));
      encode[static_cast<size_t>(i)] = static_cast<uint8_t>(std::lround(v * 255.0f));
    }
  }
  uint8_t encode_linear(float v) const {
    return encode[static_cast<size_t>(std::clamp(v, 0.0f, 1.0f) * (kEncodeSteps - 1) + 0.5f)];
  }
};

const GammaTables& gamma() {
  static const GammaTables tables;
  return tables;
}

uint8_t channel(uint32_t px, int shift) { return static_cast<uint8_t>(px >> shift & 0xFF); }

uint32_t pack(int r, int g, int b) {
  return 0xFF000000u | static_cast<uint32_t>(r) << 16 | static_cast<uint32_t>(g) << 8 |
         static_cast<uint32_t>(b);
}

// Interleaved gradient noise (Jimenez 2014): a cheap per-pixel value in [0, 1) without a
// visible pattern, used to dither float colour down to 8 bits so smooth ramps show no steps.
float dither_noise(int x, int y) {
  const float f = 0.06711056f * static_cast<float>(x) + 0.00583715f * static_cast<float>(y);
  const float g = 52.9829189f * (f - std::floor(f));
  return g - std::floor(g);
}

// Blends a float sRGB colour over a pixel with float alpha, rounding with or without dither.
uint32_t blend_float(uint32_t dst, Color c, float a, float noise) {
  const float inv = 1.0f - a;
  const float r = c.r * 255.0f * a + channel(dst, 16) * inv;
  const float g = c.g * 255.0f * a + channel(dst, 8) * inv;
  const float b = c.b * 255.0f * a + channel(dst, 0) * inv;
  auto q = [noise](float v) { return std::clamp(static_cast<int>(v + noise), 0, 255); };
  return pack(q(r), q(g), q(b));
}

// Error function approximation (Abramowitz and Stegun 7.1.27), good to about 5e-4.
float erf_approx(float x) {
  const float s = x < 0 ? -1.0f : 1.0f, a = std::fabs(x);
  float t = 1.0f + (0.278393f + (0.230389f + 0.000972f * a + 0.078108f * a * a) * a) * a;
  t *= t;
  return s - s / (t * t);
}

float gaussian(float x, float sigma) {
  return std::exp(-(x * x) / (2.0f * sigma * sigma)) / (std::sqrt(2.0f * kPi) * sigma);
}

// The rounded box blurred along x at height y; its width depends on how far into the corner
// rounding y is.
float shadow_x(float x, float y, float sigma, float corner, float half_w, float half_h) {
  const float delta = std::min(half_h - corner - std::fabs(y), 0.0f);
  const float curved = half_w - corner + std::sqrt(std::max(0.0f, corner * corner - delta * delta));
  const float k = std::sqrt(0.5f) / sigma;
  return 0.5f * (erf_approx((x + curved) * k) - erf_approx((x - curved) * k));
}

// Evan Wallace, "Fast Rounded Rectangle Shadows": exact along x, sampled along y.
float shadow_value(float px, float py, float sigma, float corner, float half_w, float half_h) {
  const float low = py - half_h, high = py + half_h;
  const float start = std::clamp(-kShadowReach * sigma, low, high);
  const float end = std::clamp(kShadowReach * sigma, low, high);
  const float step = (end - start) / kShadowSamples;
  float y = start + step * 0.5f, value = 0.0f;
  for (int i = 0; i < kShadowSamples; ++i) {
    value += shadow_x(px, py - y, sigma, corner, half_w, half_h) * gaussian(y, sigma) * step;
    y += step;
  }
  return value;
}

bool whole_pixels(RectF r) {
  return r.x == std::floor(r.x) && r.y == std::floor(r.y) && r.w == std::floor(r.w) &&
         r.h == std::floor(r.h);
}

}  // namespace

void Canvas::set_target(PixelBuffer target) {
  target_ = target;
  clip_ = {0, 0, target.width, target.height};
}

void Canvas::set_clip(RectI clip) { clip_ = clip.intersect({0, 0, target_.width, target_.height}); }

void Canvas::clear(Color c) {
  fill_rect({static_cast<float>(clip_.x), static_cast<float>(clip_.y), static_cast<float>(clip_.w),
             static_cast<float>(clip_.h)},
            c.with_alpha(1.0f));
}

void Canvas::fill_rect(RectF r, Color c) {
  if (!whole_pixels(r)) {
    path_.clear();
    path_.rounded_rect(r, 0);
    fill_path(path_, {}, c);
    return;
  }
  // Whole-pixel rectangles (backgrounds, dividers) need no coverage: store or blend directly.
  const RectI area = RectI{static_cast<int>(r.x), static_cast<int>(r.y), static_cast<int>(r.w),
                           static_cast<int>(r.h)}
                         .intersect(clip_);
  const int sr = static_cast<int>(std::lround(c.r * 255)),
            sg = static_cast<int>(std::lround(c.g * 255));
  const int sb = static_cast<int>(std::lround(c.b * 255)),
            a = static_cast<int>(std::lround(c.a * 255));
  if (a <= 0) return;
  const int inv = 255 - a;
  for (int y = area.y; y < area.bottom(); ++y) {
    uint32_t* dst = target_.row(y) + area.x;
    if (a == 255) {
      std::fill_n(dst, area.w, pack(sr, sg, sb));
      continue;
    }
    for (int x = 0; x < area.w; ++x) {
      const uint32_t d = dst[x];
      dst[x] = pack((sr * a + channel(d, 16) * inv + 127) / 255,
                    (sg * a + channel(d, 8) * inv + 127) / 255,
                    (sb * a + channel(d, 0) * inv + 127) / 255);
    }
  }
}

void Canvas::fill_rounded_rect(RectF r, float radius, Color c) {
  if (radius <= 0) {
    fill_rect(r, c);
    return;
  }
  path_.clear();
  path_.rounded_rect(r, radius);
  fill_path(path_, {}, c);
}

void Canvas::stroke_rounded_rect(RectF r, float radius, float width, Color c) {
  path_.clear();
  path_.rounded_rect(r, radius, true);
  const RectF inner{r.x + width, r.y + width, r.w - 2 * width, r.h - 2 * width};
  if (inner.w > 0 && inner.h > 0) path_.rounded_rect(inner, std::max(0.0f, radius - width), false);
  fill_path(path_, {}, c);
}

void Canvas::fill_path(const Path& path, const Transform& t, Color c) {
  blend_mask(raster_.fill(path, t, clip_), c);
}

void Canvas::stroke_polyline(std::span<const Point> pts, float width, Color c) {
  const float r = width * 0.5f;
  path_.clear();
  for (size_t i = 0; i + 1 < pts.size(); ++i) {
    const Point a = pts[i], b = pts[i + 1];
    const float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
    if (len == 0.0f) continue;
    const float nx = -dy / len * r, ny = dx / len * r;
    Point q[4] = {
        {a.x + nx, a.y + ny}, {b.x + nx, b.y + ny}, {b.x - nx, b.y - ny}, {a.x - nx, a.y - ny}};
    // Every piece must wind the same way, or overlapping pieces cancel instead of adding.
    float twice_area = 0;
    for (int k = 0; k < 4; ++k) twice_area += q[k].x * q[(k + 1) % 4].y - q[(k + 1) % 4].x * q[k].y;
    if (twice_area < 0) std::swap(q[1], q[3]);
    path_.move_to(q[0]);
    for (int k = 1; k < 4; ++k) path_.line_to(q[k]);
  }
  for (Point pt : pts) path_.rounded_rect({pt.x - r, pt.y - r, width, width}, r);  // joins and caps
  fill_path(path_, {}, c);
}

void Canvas::blend_mask(MaskView m, Color c) {
  if (m.empty()) return;
  const int sr = static_cast<int>(std::lround(c.r * 255)),
            sg = static_cast<int>(std::lround(c.g * 255));
  const int sb = static_cast<int>(std::lround(c.b * 255)),
            alpha = static_cast<int>(std::lround(c.a * 255));
  for (int y = 0; y < m.h; ++y) {
    uint32_t* dst = target_.row(m.y + y) + m.x;
    const uint8_t* cov = m.coverage + static_cast<size_t>(y) * static_cast<size_t>(m.w);
    for (int x = 0; x < m.w; ++x) {
      const int a = (alpha * cov[x] + 127) / 255;
      if (a == 0) continue;
      const uint32_t d = dst[x];
      const int inv = 255 - a;
      dst[x] = pack((sr * a + channel(d, 16) * inv + 127) / 255,
                    (sg * a + channel(d, 8) * inv + 127) / 255,
                    (sb * a + channel(d, 0) * inv + 127) / 255);
    }
  }
}

void Canvas::shadow(RectF box, float radius, float sigma, Color c, bool dither, RectF occluder) {
  // Blur and corner are snapped to 1/64 px before anything uses them, so the cached corner tile
  // for a key was made from exactly the values that look it up (and is the same size).
  sigma = std::round(sigma * kShadowKeySteps) / kShadowKeySteps;
  if (sigma <= 0 || c.a <= 0) return;
  const float reach = kShadowReach * sigma;
  const RectI area = RectI{static_cast<int>(std::floor(box.x - reach)),
                           static_cast<int>(std::floor(box.y - reach)),
                           static_cast<int>(std::ceil(box.w + 2 * reach)) + 1,
                           static_cast<int>(std::ceil(box.h + 2 * reach)) + 1}
                         .intersect(clip_);
  if (area.empty()) return;
  const float cx = box.x + box.w * 0.5f, cy = box.y + box.h * 0.5f;
  const float half_w = box.w * 0.5f, half_h = box.h * 0.5f;
  const float corner =
      std::round(std::min(radius, std::min(half_w, half_h)) * kShadowKeySteps) / kShadowKeySteps;
  const float k = std::sqrt(0.5f) / sigma;
  // Away from the corners the shadow is separable: deep inside vertically it varies only with
  // x, deep inside horizontally only with y. Those profiles are computed once per column and
  // per row; only the corner regions need the full integral.
  shadow_cols_.resize(static_cast<size_t>(area.w));
  shadow_rows_.resize(static_cast<size_t>(area.h));
  for (int x = 0; x < area.w; ++x) {
    const float px = static_cast<float>(area.x + x) + 0.5f - cx;
    shadow_cols_[static_cast<size_t>(x)] =
        0.5f * (erf_approx((px + half_w) * k) - erf_approx((px - half_w) * k));
  }
  for (int y = 0; y < area.h; ++y) {
    const float py = static_cast<float>(area.y + y) + 0.5f - cy;
    shadow_rows_[static_cast<size_t>(y)] =
        0.5f * (erf_approx((py + half_h) * k) - erf_approx((py - half_h) * k));
  }
  const float straight_x = half_w - corner - reach, straight_y = half_h - corner - reach;
  // On a whole-pixel box large enough for its corners not to overlap, each corner region is
  // the same picture, mirrored: look it up instead of integrating. The tile starts a whole
  // number of pixels outside the box, so a pixel centre's distance from an edge (always n + 0.5)
  // maps to exactly one tile column.
  const float origin = std::ceil(reach);
  const int tile = static_cast<int>(std::ceil(corner + reach + origin)) + 1;
  const std::vector<float>* corner_tile = whole_pixels(box) && straight_x > 0 && straight_y > 0
                                              ? &shadow_corner(sigma, corner, tile)
                                              : nullptr;
  // The occluder's interior, inset by the corner radius so the rounded corners stay drawn.
  const RectF hidden{occluder.x + radius, occluder.y + radius, occluder.w - 2 * radius,
                     occluder.h - 2 * radius};
  for (int y = area.y; y < area.bottom(); ++y) {
    uint32_t* dst = target_.row(y);
    const float py = static_cast<float>(y) + 0.5f - cy;
    const bool row_hidden =
        static_cast<float>(y) >= hidden.y && static_cast<float>(y + 1) <= hidden.bottom();
    for (int x = area.x; x < area.right(); ++x) {
      if (row_hidden && static_cast<float>(x) >= hidden.x &&
          static_cast<float>(x + 1) <= hidden.right()) {
        x = static_cast<int>(hidden.right()) - 1;  // jump over the covered span
        continue;
      }
      const float px = static_cast<float>(x) + 0.5f - cx;
      float v;
      if (std::fabs(py) <= straight_y) {
        v = shadow_cols_[static_cast<size_t>(x - area.x)];
      } else if (std::fabs(px) <= straight_x) {
        v = shadow_rows_[static_cast<size_t>(y - area.y)];
      } else if (corner_tile) {
        const int u = std::clamp(static_cast<int>(half_w - std::fabs(px) + origin), 0, tile - 1);
        const int w = std::clamp(static_cast<int>(half_h - std::fabs(py) + origin), 0, tile - 1);
        v = (*corner_tile)[static_cast<size_t>(w) * static_cast<size_t>(tile) +
                           static_cast<size_t>(u)];
      } else {
        v = shadow_value(px, py, sigma, corner, half_w, half_h);
      }
      const float a = std::clamp(v, 0.0f, 1.0f) * c.a;
      if (a <= 0.0f) continue;
      dst[x] = blend_float(dst[x], c, a, dither ? dither_noise(x, y) : 0.5f);
    }
  }
}

const std::vector<float>& Canvas::shadow_corner(float sigma, float corner, int size) {
  const uint64_t key = static_cast<uint64_t>(std::lround(sigma * kShadowKeySteps)) << 32 |
                       static_cast<uint64_t>(std::lround(corner * kShadowKeySteps));
  const size_t cells = static_cast<size_t>(size) * static_cast<size_t>(size);
  if (const auto it = shadow_corners_.find(key); it != shadow_corners_.end()) {
    if (it->second.size() == cells) return it->second;
    shadow_corners_.erase(it);  // cannot happen with snapped inputs; never read past a tile
  }
  // The top-left corner of a box big enough that nothing else reaches it. Index (u, v) is the
  // pixel whose centre lies u + 0.5 - origin inside the left edge and v + 0.5 - origin inside
  // the top edge, matching the lookup in shadow().
  const float origin = std::ceil(kShadowReach * sigma), half = 2.0f * static_cast<float>(size);
  std::vector<float> tile(cells);
  for (int v = 0; v < size; ++v) {
    for (int u = 0; u < size; ++u) {
      const float px = static_cast<float>(u) + 0.5f - origin - half,
                  py = static_cast<float>(v) + 0.5f - origin - half;
      tile[static_cast<size_t>(v) * static_cast<size_t>(size) + static_cast<size_t>(u)] =
          shadow_value(px, py, sigma, corner, half, half);
    }
  }
  return shadow_corners_.emplace(key, std::move(tile)).first->second;
}

void Canvas::rounded_gradient(RectF r, float radius, Color top, Color bottom, bool dither) {
  path_.clear();
  path_.rounded_rect(r, radius);
  const MaskView m = raster_.fill(path_, {}, clip_);
  for (int y = 0; y < m.h; ++y) {
    const int dy = m.y + y;
    const float t = std::clamp((static_cast<float>(dy) + 0.5f - r.y) / r.h, 0.0f, 1.0f);
    const Color c = Color::mix(top, bottom, t);
    uint32_t* dst = target_.row(dy) + m.x;
    const uint8_t* cov = m.coverage + static_cast<size_t>(y) * static_cast<size_t>(m.w);
    for (int x = 0; x < m.w; ++x) {
      if (cov[x] == 0) continue;
      const float a = c.a * static_cast<float>(cov[x]) / 255.0f;
      dst[x] = blend_float(dst[x], c, a, dither ? dither_noise(m.x + x, dy) : 0.5f);
    }
  }
}

void Canvas::blend_glyph(MaskView m, int ox, int oy, Color c, const TextTuning& tuning) {
  const GammaTables& gt = gamma();
  const float lin_r = srgb_to_linear(c.r), lin_g = srgb_to_linear(c.g), lin_b = srgb_to_linear(c.b);
  const float luminance = 0.2126f * lin_r + 0.7152f * lin_g + 0.0722f * lin_b;
  const bool linear = tuning.blend == TextBlend::linear ||
                      (tuning.blend == TextBlend::adaptive && luminance > 0.5f);
  std::array<float, 256> cov_table{};
  for (size_t i = 0; i < cov_table.size(); ++i) {
    const float v = static_cast<float>(i) / 255.0f;
    cov_table[i] = v * (1.0f + tuning.contrast) / (1.0f + tuning.contrast * v) * c.a;
  }
  const RectI dst_area = RectI{ox, oy, m.w, m.h}.intersect(clip_);
  for (int y = dst_area.y; y < dst_area.bottom(); ++y) {
    uint32_t* dst = target_.row(y);
    const uint8_t* cov = m.coverage + static_cast<size_t>(y - oy) * static_cast<size_t>(m.w);
    for (int x = dst_area.x; x < dst_area.right(); ++x) {
      const uint8_t raw = cov[x - ox];
      if (raw == 0) continue;
      const float a = cov_table[raw];
      const uint32_t d = dst[x];
      if (linear) {
        const float inv = 1.0f - a;
        dst[x] = pack(gt.encode_linear(lin_r * a + gt.decode[channel(d, 16)] * inv),
                      gt.encode_linear(lin_g * a + gt.decode[channel(d, 8)] * inv),
                      gt.encode_linear(lin_b * a + gt.decode[channel(d, 0)] * inv));
      } else {
        dst[x] = blend_float(d, c, a, 0.5f);
      }
    }
  }
}

size_t Canvas::cache_bytes() const {
  size_t bytes = 0;
  for (const auto& [key, tile] : shadow_corners_) bytes += tile.size() * sizeof(float);
  return bytes;
}

}  // namespace smalti::detail
