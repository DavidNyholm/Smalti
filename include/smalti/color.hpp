// Colours as sRGB-encoded components in 0..1 with straight (not premultiplied) alpha.
#pragma once

#include <cstdint>

namespace smalti {

struct Color {
  float r = 0, g = 0, b = 0, a = 1;

  // 0xRRGGBB, the form colours are written in design tools and theme files.
  static constexpr Color hex(uint32_t rgb, float alpha = 1.0f) {
    return {static_cast<float>(rgb >> 16 & 0xFF) / 255.0f,
            static_cast<float>(rgb >> 8 & 0xFF) / 255.0f, static_cast<float>(rgb & 0xFF) / 255.0f,
            alpha};
  }
  constexpr Color with_alpha(float alpha) const { return {r, g, b, alpha}; }
  // Straight interpolation of the encoded values, which is what CSS transitions do.
  static constexpr Color mix(Color from, Color to, float t) {
    return {from.r + (to.r - from.r) * t, from.g + (to.g - from.g) * t,
            from.b + (to.b - from.b) * t, from.a + (to.a - from.a) * t};
  }
  friend bool operator==(Color, Color) = default;
};

}  // namespace smalti
