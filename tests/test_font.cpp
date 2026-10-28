// The TrueType reader, on the embedded Inter and on damaged copies of it.
#include <cstdint>
#include <vector>

#include "check.hpp"
#include "render/text.hpp"

using namespace smalti;
using namespace smalti::detail;

namespace smalti::detail {
extern const unsigned char kInterRegular[];
extern const size_t kInterRegularSize;
}  // namespace smalti::detail

namespace {

std::vector<uint8_t> inter_bytes() { return {kInterRegular, kInterRegular + kInterRegularSize}; }

}  // namespace

TEST(font_inter_metrics_and_cmap) {
  const FontSet fonts = load_embedded_fonts();
  const Font& f = *fonts.regular;
  CHECK_EQ(f.units_per_em(), 2048);
  CHECK(f.glyph_count() > 2000);
  CHECK(f.metrics().ascender > 0);
  CHECK(f.metrics().descender < 0);
  CHECK(f.metrics().x_height > 900 && f.metrics().x_height < 1300);
  const uint16_t a = f.glyph_index('A');
  CHECK(a != 0);
  CHECK(f.glyph_index('a') != a);
  CHECK(f.glyph_index(0x10FFFF) == 0);  // not in the font: .notdef
  CHECK(f.advance(a) > 1000);
  Path outline;
  f.outline(a, outline);
  CHECK(!outline.empty());
  f.outline(f.glyph_index(' '), outline);
  CHECK(outline.empty());  // a space has no outline
}

TEST(font_inter_kerning_from_gpos) {
  // Inter has no legacy 'kern' table; these pairs come from GPOS and must tighten.
  const FontSet fonts = load_embedded_fonts();
  const Font& f = *fonts.regular;
  CHECK(f.kerning(f.glyph_index('A'), f.glyph_index('V')) < 0);
  CHECK(f.kerning(f.glyph_index('T'), f.glyph_index('o')) < 0);
  CHECK_EQ(f.kerning(f.glyph_index('l'), f.glyph_index('l')), 0);
}

TEST(font_rejects_what_is_not_truetype) {
  const std::vector<uint8_t> junk{'O', 'T', 'T', 'O', 0, 0, 0, 0, 0, 0, 0, 0};
  bool threw = false;
  try {
    Font f(junk);
  } catch (const FontError&) {
    threw = true;
  }
  CHECK(threw);
  threw = false;
  try {
    Font f(std::vector<uint8_t>{});
  } catch (const FontError&) {
    threw = true;
  }
  CHECK(threw);
}

TEST(font_damaged_copies_fail_cleanly_or_still_work) {
  // Truncations and flipped bytes across the whole file: each must either be refused with a
  // FontError or load and answer queries without reading out of bounds. (Under the
  // sanitizers, an out-of-bounds read fails this test loudly.)
  const std::vector<uint8_t> good = inter_bytes();
  int refused = 0, loaded = 0;
  for (size_t cut = 0; cut < good.size(); cut += good.size() / 97 + 1) {
    std::vector<uint8_t> bytes(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(cut));
    try {
      Font f(std::move(bytes));
      Path p;
      for (char32_t c : {U'A', U'g', U'%', char32_t{0xE9}}) {
        f.outline(f.glyph_index(c), p);
        (void)f.advance(f.glyph_index(c));
      }
      (void)f.kerning(f.glyph_index('A'), f.glyph_index('V'));
      ++loaded;
    } catch (const FontError&) {
      ++refused;
    }
  }
  uint32_t seed = 12345;
  for (int round = 0; round < 200; ++round) {
    std::vector<uint8_t> bytes = good;
    for (int k = 0; k < 16; ++k) {
      seed = seed * 1664525u + 1013904223u;
      bytes[seed % bytes.size()] = static_cast<uint8_t>(seed >> 24);
    }
    try {
      Font f(std::move(bytes));
      Path p;
      for (char32_t c = 32; c < 128; ++c) f.outline(f.glyph_index(c), p);
      (void)f.kerning(f.glyph_index('A'), f.glyph_index('V'));
      ++loaded;
    } catch (const FontError&) {
      ++refused;
    }
  }
  CHECK(refused > 0);
  CHECK(loaded > 0);
}
