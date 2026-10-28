// A TrueType (glyf) font reader: character map, metrics, outlines and GPOS pair kerning.
// Every read of font data is bounds-checked, because a font file is input.
#pragma once

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "render/geometry.hpp"

namespace smalti::detail {

struct FontError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// Big-endian reads that throw FontError instead of reading past the data.
class Bytes {
 public:
  Bytes() = default;
  explicit Bytes(std::span<const uint8_t> data) : data_(data) {}

  size_t size() const { return data_.size(); }
  uint8_t u8(size_t off) const;
  uint16_t u16(size_t off) const;
  int16_t i16(size_t off) const { return static_cast<int16_t>(u16(off)); }
  uint32_t u32(size_t off) const;
  Bytes sub(size_t off, size_t len) const;
  Bytes from(size_t off) const;

 private:
  void check(size_t off, size_t len) const;
  std::span<const uint8_t> data_;
};

struct LineMetrics {
  int ascender = 0;   // font units above the baseline (positive)
  int descender = 0;  // font units below the baseline (negative)
  int line_gap = 0;
  int x_height = 0;
  int cap_height = 0;
};

class Font {
 public:
  // Reads font data that outlives the Font (such as the embedded fonts). Throws FontError if
  // it is not a TrueType font with glyf outlines.
  explicit Font(std::span<const uint8_t> data);
  // Reads font data the Font keeps.
  explicit Font(std::vector<uint8_t> data);
  Font(const Font&) = delete;
  Font& operator=(const Font&) = delete;

  int units_per_em() const { return units_per_em_; }
  const LineMetrics& metrics() const { return metrics_; }
  uint16_t glyph_count() const { return glyph_count_; }

  uint16_t glyph_index(char32_t cp) const;           // 0 (.notdef) if missing
  int advance(uint16_t glyph) const;                 // font units
  int kerning(uint16_t left, uint16_t right) const;  // font units, added to left's advance
  // Replaces `out` with the glyph's outline in font units (y up); empty for damaged data.
  void outline(uint16_t glyph, Path& out) const;

 private:
  void parse();
  Bytes table(const char* tag, bool required) const;
  void read_cmap();
  void read_kerning_lookups();
  struct OutlineBudget {
    int components;  // left to visit
    size_t points;   // left to read
  };
  void append_outline(Path& path, uint16_t glyph, const Transform& t, int depth,
                      OutlineBudget& budget) const;
  void append_simple(Path& path, Bytes glyph, int contours, const Transform& t,
                     OutlineBudget& budget) const;
  void append_composite(Path& path, Bytes glyph, const Transform& t, int depth,
                        OutlineBudget& budget) const;
  int pair_adjustment(uint32_t subtable, uint16_t left, uint16_t right, bool& applied) const;

  std::vector<uint8_t> owned_;
  Bytes bytes_;
  std::unordered_map<std::string, std::pair<uint32_t, uint32_t>> tables_;  // tag -> offset, length

  int units_per_em_ = 0;
  uint16_t glyph_count_ = 0;
  bool long_loca_ = false;
  uint16_t h_metric_count_ = 0;
  LineMetrics metrics_;
  uint32_t glyf_offset_ = 0, loca_offset_ = 0, hmtx_offset_ = 0;

  uint32_t cmap_offset_ = 0;  // the chosen subtable, from the start of the font data
  uint16_t cmap_format_ = 0;

  std::vector<uint32_t> pair_subtables_;  // GPOS pair adjustments of the 'kern' feature
  std::vector<size_t> pair_lookup_ends_;  // where each lookup's subtables end in pair_subtables_
  mutable std::unordered_map<uint32_t, int16_t> kerning_cache_;
};

}  // namespace smalti::detail
