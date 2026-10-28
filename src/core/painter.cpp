// Painter: logical-pixel drawing over the device-pixel canvas.
#include "smalti/painter.hpp"

#include <algorithm>
#include <cmath>

#include "core/state.hpp"
#include "render/text.hpp"

namespace smalti {

namespace {

// Logical rectangle to device pixels, edges on whole pixels so hairlines stay crisp.
detail::RectF snap(Rect r, float s) {
  const float x0 = std::round(r.x * s), y0 = std::round(r.y * s);
  return {x0, y0, std::round(r.right() * s) - x0, std::round(r.bottom() * s) - y0};
}

}  // namespace

float Painter::scale() const { return s_.scale; }

float Painter::hairline() const { return 1.0f / s_.scale; }

void Painter::fill_rect(Rect r, Color c) { s_.canvas->fill_rect(snap(r, s_.scale), c); }

void Painter::fill_rounded_rect(Rect r, float radius, Color c) {
  s_.canvas->fill_rounded_rect(snap(r, s_.scale), radius * s_.scale, c);
}

void Painter::stroke_rounded_rect(Rect r, float radius, float width, Color c) {
  const float w = width > 0 ? width * s_.scale : std::max(1.0f, std::floor(s_.scale));
  s_.canvas->stroke_rounded_rect(snap(r, s_.scale), radius * s_.scale, w, c);
}

void Painter::fill_circle(Point center, float radius, Color c) {
  const float s = s_.scale;
  s_.canvas->fill_rounded_rect({(center.x - radius) * s, (center.y - radius) * s, 2 * radius * s, 2 * radius * s},
                               radius * s, c);
}

void Painter::stroke_circle(Point center, float radius, float width, Color c) {
  const float s = s_.scale;
  s_.canvas->stroke_rounded_rect({(center.x - radius) * s, (center.y - radius) * s, 2 * radius * s, 2 * radius * s},
                                 radius * s, width * s, c);
}

void Painter::polyline(std::span<const Point> points, float width, Color c) {
  s_.scratch.clear();
  for (Point p : points) s_.scratch.push_back({p.x * s_.scale, p.y * s_.scale});
  s_.canvas->stroke_polyline(s_.scratch, width * s_.scale, c);
}

void Painter::shadow(Rect box, float radius, float blur, Color c, Rect surface) {
  const detail::RectF occluder = surface.empty() ? detail::RectF{} : snap(surface, s_.scale);
  s_.canvas->shadow(snap(box, s_.scale), radius * s_.scale, blur * s_.scale, c, true, occluder);
}

void Painter::vertical_gradient(Rect r, float radius, Color top, Color bottom) {
  s_.canvas->rounded_gradient(snap(r, s_.scale), radius * s_.scale, top, bottom, true);
}

float Painter::text(Point baseline, std::string_view utf8, const TextStyle& style, Color c) {
  const detail::Font& font = s_.fonts->get(style.weight);
  const float end = detail::draw_text(*s_.canvas, *s_.glyphs, font, style.size * s_.scale,
                                      {baseline.x * s_.scale, std::round(baseline.y * s_.scale)}, utf8, c,
                                      s_.theme->text_tuning);
  return end / s_.scale;
}

float Painter::text_width(std::string_view utf8, const TextStyle& style) const {
  return detail::text_width(s_.fonts->get(style.weight), style.size, utf8);
}

float Painter::ascent(const TextStyle& style) const {
  const detail::Font& f = s_.fonts->get(style.weight);
  return static_cast<float>(f.metrics().ascender) * style.size / static_cast<float>(f.units_per_em());
}

float Painter::descent(const TextStyle& style) const {
  const detail::Font& f = s_.fonts->get(style.weight);
  return static_cast<float>(-f.metrics().descender) * style.size / static_cast<float>(f.units_per_em());
}

float Painter::line_height(const TextStyle& style) const {
  const detail::Font& f = s_.fonts->get(style.weight);
  return static_cast<float>(f.metrics().ascender - f.metrics().descender + f.metrics().line_gap) * style.size /
         static_cast<float>(f.units_per_em());
}

void Painter::push_clip(Rect r) {
  const float s = s_.scale;
  const int x0 = static_cast<int>(std::floor(r.x * s)), y0 = static_cast<int>(std::floor(r.y * s));
  const detail::RectI device{x0, y0, static_cast<int>(std::ceil(r.right() * s)) - x0,
                             static_cast<int>(std::ceil(r.bottom() * s)) - y0};
  const detail::RectI clip = device.intersect(s_.clips.empty() ? s_.canvas->clip() : s_.clips.back());
  s_.clips.push_back(clip);
  s_.canvas->set_clip(clip);
}

void Painter::pop_clip() {
  if (s_.clips.size() <= 1) return;  // the outermost clip is the damaged rectangle itself
  s_.clips.pop_back();
  s_.canvas->set_clip(s_.clips.back());
}

}  // namespace smalti
