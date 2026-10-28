// Builds small TrueType fonts in memory, so tests can hand the reader exactly the structure a
// hostile or unusual font would have. Only what Font reads is written; checksums are zero.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace font_builder {

using Bytes = std::vector<uint8_t>;

inline void u16(Bytes& b, uint32_t v) {
  b.push_back(static_cast<uint8_t>(v >> 8));
  b.push_back(static_cast<uint8_t>(v));
}

inline void u32(Bytes& b, uint32_t v) {
  u16(b, v >> 16);
  u16(b, v & 0xFFFF);
}

inline void i16(Bytes& b, int v) { u16(b, static_cast<uint16_t>(static_cast<int16_t>(v))); }

inline void tag(Bytes& b, const char* t) {
  for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(t[i]));
}

// One contour of on-curve points, as a simple glyph.
inline Bytes simple_glyph(const std::vector<std::pair<int, int>>& points) {
  Bytes g;
  i16(g, 1);                                                     // one contour
  for (int i = 0; i < 4; ++i) i16(g, 0);                         // bounding box: not read
  u16(g, static_cast<uint32_t>(points.size() - 1));              // its last point
  u16(g, 0);                                                     // no instructions
  for (size_t i = 0; i < points.size(); ++i) g.push_back(0x01);  // on curve, x and y as words
  int last = 0;
  for (const auto& p : points) i16(g, p.first - std::exchange(last, p.first));
  last = 0;
  for (const auto& p : points) i16(g, p.second - std::exchange(last, p.second));
  return g;
}

// A composite glyph made of `copies` unmoved copies of `component`.
inline Bytes composite_glyph(uint16_t component, int copies) {
  constexpr uint16_t kArgsAreWords = 0x0001, kArgsAreXY = 0x0002, kMoreComponents = 0x0020;
  Bytes g;
  i16(g, -1);
  for (int i = 0; i < 4; ++i) i16(g, 0);
  for (int i = 0; i < copies; ++i) {
    u16(g, kArgsAreWords | kArgsAreXY | (i + 1 < copies ? kMoreComponents : 0));
    u16(g, component);
    i16(g, 0);
    i16(g, 0);
  }
  return g;
}

struct CmapGroup {
  uint32_t first, last, glyph;
};

// A format 12 character map. `claimed_groups` is what the header says, which a damaged font
// may get wrong; by default it is the true count.
inline Bytes cmap12(const std::vector<CmapGroup>& groups, uint32_t claimed_groups = UINT32_MAX) {
  Bytes c;
  u16(c, 0);   // version
  u16(c, 1);   // one encoding record
  u16(c, 3);   // Windows
  u16(c, 10);  // full Unicode
  u32(c, 12);  // the subtable follows
  u16(c, 12);  // format
  u16(c, 0);
  u32(c, static_cast<uint32_t>(16 + 12 * groups.size()));
  u32(c, 0);  // language
  u32(c, claimed_groups == UINT32_MAX ? static_cast<uint32_t>(groups.size()) : claimed_groups);
  for (const CmapGroup& g : groups) {
    u32(c, g.first);
    u32(c, g.last);
    u32(c, g.glyph);
  }
  return c;
}

struct KernPair {
  uint16_t left, right;
  int x_advance;
};

// A GPOS table whose 'kern' feature has one lookup per pair, each a single format 1 pair
// adjustment: every lookup applies on its own, so their adjustments add up.
inline Bytes gpos_kern(const std::vector<KernPair>& lookups) {
  const size_t n = lookups.size();
  Bytes g;
  u16(g, 1);
  u16(g, 0);
  u16(g, 10);  // script list: not read, but present and empty
  const size_t feature_list = 12;
  u16(g, static_cast<uint32_t>(feature_list));
  const size_t feature_list_size = 2 + 6 + 4 + 2 * n;
  const size_t lookup_list = feature_list + feature_list_size;
  u16(g, static_cast<uint32_t>(lookup_list));
  u16(g, 0);  // the script list: no scripts
  // Feature list: one 'kern' feature using every lookup.
  u16(g, 1);
  tag(g, "kern");
  u16(g, 8);  // the feature table, from the feature list
  u16(g, 0);  // no feature parameters
  u16(g, static_cast<uint32_t>(n));
  for (size_t i = 0; i < n; ++i) u16(g, static_cast<uint32_t>(i));
  // Lookup list: each lookup holds one pair adjustment subtable.
  constexpr size_t kLookupSize = 8, kSubtableSize = 12 + 2 + 4 + 6;
  u16(g, static_cast<uint32_t>(n));
  const size_t first_lookup = 2 + 2 * n;
  for (size_t i = 0; i < n; ++i)
    u16(g, static_cast<uint32_t>(first_lookup + i * (kLookupSize + kSubtableSize)));
  for (const KernPair& k : lookups) {
    u16(g, 2);            // pair adjustment
    u16(g, 0);            // flags
    u16(g, 1);            // one subtable
    u16(g, kLookupSize);  // right after this lookup
    // PairPos format 1: coverage of the left glyph, one pair set holding the right glyph.
    u16(g, 1);
    u16(g, 18);      // coverage, after the pair set
    u16(g, 0x0004);  // first value record: x advance only
    u16(g, 0);       // no second value record
    u16(g, 1);       // one pair set
    u16(g, 12);      // right after this header
    u16(g, 1);       // pair set: one pair
    u16(g, k.right);
    i16(g, k.x_advance);
    u16(g, 1);  // coverage format 1, one glyph
    u16(g, 1);
    u16(g, k.left);
  }
  return g;
}

// A GPOS table whose feature list repeats one 'kern' record `records` times, every copy pointing
// at the same feature table, which lists `lookups` lookup indices. There are no lookups at all:
// a reader that does not notice the repetition does records * lookups work for nothing.
inline Bytes gpos_repeated_feature(uint16_t records, uint16_t lookups) {
  Bytes g;
  u16(g, 1);
  u16(g, 0);
  u16(g, 10);  // script list: present and empty
  constexpr size_t kFeatureList = 12;
  const size_t feature_table = 2 + 6 * size_t{records};  // from the feature list
  const size_t lookup_list = kFeatureList + feature_table + 4 + 2 * size_t{lookups};
  u16(g, static_cast<uint32_t>(kFeatureList));
  u16(g, static_cast<uint32_t>(lookup_list));
  u16(g, 0);
  u16(g, records);
  for (uint16_t i = 0; i < records; ++i) {
    tag(g, "kern");
    u16(g, static_cast<uint32_t>(feature_table));
  }
  u16(g, 0);  // no feature parameters
  u16(g, lookups);
  for (uint16_t i = 0; i < lookups; ++i) u16(g, i);
  u16(g, 0);  // the lookup list: empty
  return g;
}

// The tables a font needs around `glyphs` (glyph 0 should be the empty .notdef). `extra` adds or
// replaces tables, by tag.
inline Bytes font(const std::vector<Bytes>& glyphs, const std::map<std::string, Bytes>& extra,
                  uint16_t units_per_em = 1000) {
  std::map<std::string, Bytes> tables;
  const auto count = static_cast<uint32_t>(glyphs.size());
  Bytes& head = tables["head"];
  head.resize(54, 0);
  head[18] = static_cast<uint8_t>(units_per_em >> 8);
  head[19] = static_cast<uint8_t>(units_per_em & 0xFF);
  head[51] = 1;  // long loca offsets
  Bytes& maxp = tables["maxp"];
  u32(maxp, 0x00005000);
  u16(maxp, count);
  Bytes& hhea = tables["hhea"];
  hhea.resize(34, 0);
  u16(hhea, count);  // a metric for every glyph
  Bytes& hmtx = tables["hmtx"];
  for (uint32_t i = 0; i < count; ++i) {
    u16(hmtx, 500);
    i16(hmtx, 0);
  }
  Bytes& glyf = tables["glyf"];
  Bytes& loca = tables["loca"];
  for (const Bytes& g : glyphs) {
    u32(loca, static_cast<uint32_t>(glyf.size()));
    glyf.insert(glyf.end(), g.begin(), g.end());
    while (glyf.size() % 4) glyf.push_back(0);
  }
  u32(loca, static_cast<uint32_t>(glyf.size()));
  for (const auto& [t, b] : extra) tables[t] = b;

  Bytes out;
  u32(out, 0x00010000);
  u16(out, static_cast<uint32_t>(tables.size()));
  for (int i = 0; i < 3; ++i) u16(out, 0);  // search hints: not read
  size_t offset = 12 + 16 * tables.size();
  for (const auto& [t, b] : tables) {
    tag(out, t.c_str());
    u32(out, 0);
    u32(out, static_cast<uint32_t>(offset));
    u32(out, static_cast<uint32_t>(b.size()));
    offset += (b.size() + 3) / 4 * 4;
  }
  for (const auto& [t, b] : tables) {
    out.insert(out.end(), b.begin(), b.end());
    while (out.size() % 4) out.push_back(0);
  }
  return out;
}

}  // namespace font_builder
