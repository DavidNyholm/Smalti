// Drawing and measuring helpers shared by the built-in widgets, so they look like one family.
#pragma once

#include <string_view>

#include "core/state.hpp"
#include "smalti/painter.hpp"
#include "smalti/theme.hpp"
#include "theme/widget_styles.hpp"

namespace smalti::detail {

// Space between a control's mark (box, track, ring) and its text.
inline constexpr float kLabelGap = 10;
// A focus ring sits this far outside the control.
inline constexpr float kFocusRingGap = 3;
inline constexpr float kFocusRingWidth = 2;

inline float measure_text(std::string_view text, const TextStyle& style) {
  return detail::text_width(app_state().fonts.get(style.weight), style.size, text);
}

inline float text_ascent(const TextStyle& style) {
  const Font& f = app_state().fonts.get(style.weight);
  return static_cast<float>(f.metrics().ascender) * style.size /
         static_cast<float>(f.units_per_em());
}

inline float text_descent(const TextStyle& style) {
  const Font& f = app_state().fonts.get(style.weight);
  return static_cast<float>(-f.metrics().descender) * style.size /
         static_cast<float>(f.units_per_em());
}

inline float text_line_height(const TextStyle& style) {
  const Font& f = app_state().fonts.get(style.weight);
  const LineMetrics& m = f.metrics();
  return static_cast<float>(m.ascender - m.descender + m.line_gap) * style.size /
         static_cast<float>(f.units_per_em());
}

// The baseline that centres one line of `style` vertically in `r`.
inline float centered_baseline(Rect r, const TextStyle& style) {
  const float a = text_ascent(style), d = text_descent(style);
  return r.y + (r.height - (a + d)) * 0.5f + a;
}

// The style a built-in widget draws with: the theme's, computed when it was set.
inline const WidgetStyle& widget_style(StyledWidget w, size_t variant = 0) {
  return app_state().styles.get(w, variant);
}

inline void focus_ring(Painter& p, Rect r, float radius, Color color) {
  const float g = kFocusRingGap;
  p.stroke_rounded_rect({r.x - g, r.y - g, r.width + 2 * g, r.height + 2 * g}, radius + g,
                        kFocusRingWidth, color);
}

inline TextStyle body_text(const Theme& th, FontWeight weight = FontWeight::regular) {
  return {th.text_body, weight};
}

}  // namespace smalti::detail
