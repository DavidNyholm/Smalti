#include "render/text.hpp"

#include <algorithm>
#include <cmath>
#include <functional>

namespace smalti::detail {

namespace {

constexpr char32_t kReplacement = 0xFFFD;
constexpr char32_t kMaxCodepoint = 0x10FFFF;
constexpr char32_t kSurrogateFirst = 0xD800, kSurrogateLast = 0xDFFF;
// The smallest value each sequence length may encode; anything less is an overlong form.
constexpr char32_t kMinForExtraBytes[] = {0, 0x80, 0x800, 0x10000};
// No glyph a real font draws at a text size comes near this many pixels; only a hostile font
// asks for more, and its coverage alone could take gigabytes.
constexpr double kMaxGlyphPixels = 1024.0 * 1024.0;
// Glyphs are positioned to a quarter of a pixel horizontally; finer steps are not visible.
constexpr int kSubpixelSteps = 4;
// Snapping the x-height may stretch or squash text by at most this much, so unusual sizes are
// left alone rather than distorted.
constexpr float kMaxSnapStretch = 0.08f;
constexpr int kUnbounded = 1 << 20;

float x_height_scale(const Font& font, float size) {
  const float x_height =
      static_cast<float>(font.metrics().x_height) * size / static_cast<float>(font.units_per_em());
  const float snapped = std::round(x_height);
  if (snapped < 1.0f) return 1.0f;
  const float scale = snapped / x_height;
  return std::fabs(scale - 1.0f) <= kMaxSnapStretch ? scale : 1.0f;
}

bool is_space(char32_t cp) { return cp == ' ' || cp == '\t'; }

// Whether the outline, placed by t, covers few enough pixels to rasterize: checked before the
// rasterizer allocates for it.
bool within_raster_limit(const Path& outline, const Transform& t) {
  if (outline.points().empty()) return true;
  double x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
  for (const Point p : outline.points()) {
    const Point d = t.apply(p);
    x0 = std::min<double>(x0, d.x);
    y0 = std::min<double>(y0, d.y);
    x1 = std::max<double>(x1, d.x);
    y1 = std::max<double>(y1, d.y);
  }
  // Far outside any window, coordinates no longer fit the rasterizer's integers, whatever the
  // area (a degenerate glyph has none). Written so that NaN fails too.
  const double reach = kUnbounded;
  if (!(x0 >= -reach && x1 <= reach && y0 >= -reach && y1 <= reach)) return false;
  const double area = (x1 - x0 + 1) * (y1 - y0 + 1);
  return std::isfinite(area) && area <= kMaxGlyphPixels;
}

}  // namespace

const Font& FontSet::get(FontWeight weight) const {
  switch (weight) {
    case FontWeight::medium:
      return *medium;
    case FontWeight::semibold:
      return *semibold;
    case FontWeight::regular:
      break;
  }
  return *regular;
}

char32_t next_codepoint(std::string_view s, size_t& i) {
  const auto b0 = static_cast<uint8_t>(s[i++]);
  if (b0 < 0x80) return b0;
  int extra = 0;
  char32_t cp = 0;
  if ((b0 & 0xE0) == 0xC0) {
    extra = 1;
    cp = b0 & 0x1F;
  } else if ((b0 & 0xF0) == 0xE0) {
    extra = 2;
    cp = b0 & 0x0F;
  } else if ((b0 & 0xF8) == 0xF0) {
    extra = 3;
    cp = b0 & 0x07;
  } else {
    return kReplacement;
  }
  for (int k = 0; k < extra; ++k) {
    if (i >= s.size() || (static_cast<uint8_t>(s[i]) & 0xC0) != 0x80) return kReplacement;
    cp = cp << 6 | (static_cast<uint8_t>(s[i++]) & 0x3F);
  }
  // Overlong forms, UTF-16 surrogates and values past the last code point are not characters.
  if (cp < kMinForExtraBytes[extra] || (cp >= kSurrogateFirst && cp <= kSurrogateLast) ||
      cp > kMaxCodepoint) {
    return kReplacement;
  }
  return cp;
}

size_t GlyphCache::KeyHash::operator()(const Key& k) const {
  size_t h = std::hash<const void*>{}(k.font);
  h = h * 31 + k.glyph;
  h = h * 31 + static_cast<size_t>(k.size_64ths);
  h = h * 31 + static_cast<size_t>(k.quarter);
  return h * 31 + static_cast<size_t>(k.y_scale_1024ths);
}

MaskView GlyphCache::get(const Font& font, uint16_t glyph, float size, int quarter, float y_scale) {
  const Key key{&font, glyph, static_cast<int>(std::lround(size * 64)), quarter,
                static_cast<int>(std::lround(y_scale * 1024))};
  if (const auto it = index_.find(key); it != index_.end()) {
    lru_.splice(lru_.begin(), lru_, it->second);  // most recently used: no allocation
    const Entry& e = *it->second;
    return {e.x, e.y, e.w, e.h, e.coverage.data()};
  }
  const float scale = size / static_cast<float>(font.units_per_em());
  // Font units are y-up; device pixels are y-down with the baseline at y = 0.
  const Transform t{scale, -scale * y_scale, static_cast<float>(quarter) / kSubpixelSteps, 0.0f};
  font.outline(glyph, outline_);
  const MaskView m =
      within_raster_limit(outline_, t)
          ? raster_.fill(outline_, t, {-kUnbounded, -kUnbounded, 2 * kUnbounded, 2 * kUnbounded})
          : MaskView{};  // drawn as nothing, like a damaged glyph
  Entry entry{key, m.x, m.y, m.w, m.h, {}};
  if (!m.empty())
    entry.coverage.assign(m.coverage,
                          m.coverage + static_cast<size_t>(m.w) * static_cast<size_t>(m.h));
  bytes_ += cost(entry);
  lru_.push_front(std::move(entry));
  index_.emplace(key, lru_.begin());
  // Keep the newest entry even if it alone exceeds the budget; evict from the cold end.
  while (bytes_ > budget_ && lru_.size() > 1) {
    const Entry& cold = lru_.back();
    bytes_ -= cost(cold);
    index_.erase(cold.key);
    lru_.pop_back();
  }
  const Entry& e = lru_.front();
  return {e.x, e.y, e.w, e.h, e.coverage.data()};
}

float text_width(const Font& font, float size, std::string_view utf8) {
  const float scale = size / static_cast<float>(font.units_per_em());
  float width = 0;
  uint16_t prev = 0;
  for (size_t i = 0; i < utf8.size();) {
    const uint16_t g = font.glyph_index(next_codepoint(utf8, i));
    if (prev) width += static_cast<float>(font.kerning(prev, g)) * scale;
    width += static_cast<float>(font.advance(g)) * scale;
    prev = g;
  }
  return width;
}

float draw_text(Canvas& canvas, GlyphCache& cache, const Font& font, float size, Point origin,
                std::string_view utf8, Color color, const TextTuning& tuning) {
  const float scale = size / static_cast<float>(font.units_per_em());
  const float y_scale = tuning.snap_x_height ? x_height_scale(font, size) : 1.0f;
  const int baseline = static_cast<int>(std::lround(origin.y));
  float pen = origin.x;
  uint16_t prev = 0;
  for (size_t i = 0; i < utf8.size();) {
    const uint16_t g = font.glyph_index(next_codepoint(utf8, i));
    if (prev) pen += static_cast<float>(font.kerning(prev, g)) * scale;
    int whole = static_cast<int>(std::floor(pen));
    int quarter = static_cast<int>(std::lround((pen - static_cast<float>(whole)) * kSubpixelSteps));
    if (quarter == kSubpixelSteps) {
      ++whole;
      quarter = 0;
    }
    const MaskView m = cache.get(font, g, size, quarter, y_scale);
    if (!m.empty()) canvas.blend_glyph(m, whole + m.x, baseline + m.y, color, tuning);
    pen += static_cast<float>(font.advance(g)) * scale;
    prev = g;
  }
  return pen;
}

std::vector<std::string_view> wrap_lines(const Font& font, float size, std::string_view utf8,
                                         float max_width) {
  const float scale = size / static_cast<float>(font.units_per_em());
  std::vector<std::string_view> lines;
  size_t line_start = 0, last_space = std::string_view::npos, i = 0;
  float width = 0;
  uint16_t prev = 0;
  auto start_line = [&](size_t at) {
    line_start = i = at;
    width = 0;
    prev = 0;
    last_space = std::string_view::npos;
  };
  while (i < utf8.size()) {
    if (utf8[i] == '\n') {
      lines.push_back(utf8.substr(line_start, i - line_start));
      start_line(i + 1);
      continue;
    }
    size_t next = i;
    const char32_t cp = next_codepoint(utf8, next);
    const uint16_t g = font.glyph_index(cp);
    const float w =
        static_cast<float>((prev ? font.kerning(prev, g) : 0) + font.advance(g)) * scale;
    if (is_space(cp)) last_space = i;
    // Spaces may hang past the edge; anything else that does not fit starts a new line, at the
    // last space if there is one, or between characters inside a word too long for a line.
    if (!is_space(cp) && width + w > max_width && i > line_start) {
      const size_t end =
          last_space != std::string_view::npos && last_space > line_start ? last_space : i;
      lines.push_back(utf8.substr(line_start, end - line_start));
      size_t resume = end;
      while (resume < utf8.size() && (utf8[resume] == ' ' || utf8[resume] == '\t')) ++resume;
      start_line(resume);
      continue;
    }
    width += w;
    prev = g;
    i = next;
  }
  lines.push_back(utf8.substr(line_start));
  return lines;
}

}  // namespace smalti::detail
