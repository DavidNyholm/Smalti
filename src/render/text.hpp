// Text: UTF-8 to glyphs, kerning, quarter-pixel positioning, a glyph cache with a memory
// budget, and line breaking. Left to right with pair kerning only: Latin, Greek and Cyrillic.
#pragma once

#include <list>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "render/canvas.hpp"
#include "render/font.hpp"
#include "smalti/painter.hpp"

namespace smalti::detail {

// The fonts compiled into the library (Inter).
struct FontSet {
  std::unique_ptr<Font> regular, medium, semibold;
  const Font& get(FontWeight weight) const;
};
FontSet load_embedded_fonts();

class GlyphCache {
 public:
  static constexpr size_t kDefaultBudget = 4u << 20;  // 4 MB: thousands of glyphs at UI sizes
  explicit GlyphCache(size_t budget_bytes = kDefaultBudget) : budget_(budget_bytes) {}

  // The glyph's coverage relative to the pen's whole pixel on the baseline. Valid until the
  // next call, which may evict it.
  MaskView get(const Font& font, uint16_t glyph, float size, int quarter, float y_scale);
  size_t bytes() const { return bytes_; }  // coverage plus each entry's own bookkeeping
  size_t entries() const { return lru_.size(); }

 private:
  struct Key {
    const Font* font;
    uint16_t glyph;
    int size_64ths;
    int quarter;
    int y_scale_1024ths;
    bool operator==(const Key&) const = default;
  };
  struct KeyHash {
    size_t operator()(const Key& k) const;
  };
  struct Entry {
    Key key;
    int x, y, w, h;
    std::vector<uint8_t> coverage;
  };
  // What an entry costs besides its coverage: itself, its list node and its index node (about;
  // the exact node layout is the standard library's). Counting it bounds the number of entries
  // too, so even glyphs with no pixels (spaces) cannot grow the cache without limit.
  static constexpr size_t kEntryOverhead = sizeof(Entry) + sizeof(Key) + 6 * sizeof(void*);
  static size_t cost(const Entry& e) { return e.coverage.size() + kEntryOverhead; }
  std::list<Entry> lru_;  // most recently used first
  std::unordered_map<Key, std::list<Entry>::iterator, KeyHash> index_;
  Rasterizer raster_;
  Path outline_;
  size_t bytes_ = 0;
  size_t budget_;
};

float text_width(const Font& font, float size, std::string_view utf8);
// Draws one line with its baseline at origin.y (device pixels); returns the pen's final x.
float draw_text(Canvas& canvas, GlyphCache& cache, const Font& font, float size, Point origin,
                std::string_view utf8, Color color, const TextTuning& tuning);

// Splits `utf8` into lines no wider than `max_width` (device pixels for `size`), breaking at
// spaces; a word longer than a line is broken between characters. Explicit newlines break too.
std::vector<std::string_view> wrap_lines(const Font& font, float size, std::string_view utf8,
                                         float max_width);

// Decodes one UTF-8 sequence at s[i], advancing i. Invalid bytes become U+FFFD.
char32_t next_codepoint(std::string_view s, size_t& i);

}  // namespace smalti::detail
