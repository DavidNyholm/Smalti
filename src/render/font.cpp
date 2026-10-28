#include "render/font.hpp"

#include <algorithm>
#include <bit>

namespace smalti::detail {

namespace {

// A composite glyph may refer to composites; real fonts nest two or three deep. The limit
// stops a damaged font that refers to itself.
constexpr int kMaxCompositeDepth = 8;

// GPOS lookup types and the value-record bits we read.
constexpr uint16_t kLookupPairAdjustment = 2;
constexpr uint16_t kLookupExtension = 9;
constexpr uint16_t kValueXPlacement = 0x0001;
constexpr uint16_t kValueYPlacement = 0x0002;
constexpr uint16_t kValueXAdvance = 0x0004;

// Simple-glyph point flags.
constexpr uint8_t kOnCurve = 0x01;
constexpr uint8_t kXShort = 0x02;
constexpr uint8_t kYShort = 0x04;
constexpr uint8_t kRepeat = 0x08;
constexpr uint8_t kXSameOrPositive = 0x10;
constexpr uint8_t kYSameOrPositive = 0x20;

// Composite-glyph component flags.
constexpr uint16_t kArgsAreWords = 0x0001;
constexpr uint16_t kArgsAreXY = 0x0002;
constexpr uint16_t kHaveScale = 0x0008;
constexpr uint16_t kMoreComponents = 0x0020;
constexpr uint16_t kHaveXYScale = 0x0040;
constexpr uint16_t kHaveTwoByTwo = 0x0080;

constexpr float kF2Dot14 = 1.0f / 16384.0f;
constexpr size_t kOs2V2Size = 96;  // sxHeight and sCapHeight exist from OS/2 version 2 on
constexpr int kMinUnitsPerEm = 16;

int value_record_size(uint16_t format) { return 2 * std::popcount(static_cast<unsigned>(format & 0xFF)); }

// Offset of XAdvance inside a value record, or -1 if the record has none.
int x_advance_offset(uint16_t format) {
  if (!(format & kValueXAdvance)) return -1;
  return 2 * std::popcount(static_cast<unsigned>(format & (kValueXPlacement | kValueYPlacement)));
}

// Index of `glyph` in an OpenType Coverage table, or -1.
int coverage_index(Bytes cov, uint16_t glyph) {
  const uint16_t format = cov.u16(0);
  int lo = 0, hi = cov.u16(2) - 1;
  if (format == 1) {
    while (lo <= hi) {
      const int mid = (lo + hi) / 2;
      const uint16_t g = cov.u16(4 + 2 * static_cast<size_t>(mid));
      if (g == glyph) return mid;
      if (g < glyph) lo = mid + 1;
      else hi = mid - 1;
    }
  } else if (format == 2) {
    while (lo <= hi) {
      const int mid = (lo + hi) / 2;
      const size_t rec = 4 + 6 * static_cast<size_t>(mid);
      if (glyph < cov.u16(rec)) hi = mid - 1;
      else if (glyph > cov.u16(rec + 2)) lo = mid + 1;
      else return cov.u16(rec + 4) + (glyph - cov.u16(rec));
    }
  }
  return -1;
}

// Class of `glyph` in an OpenType ClassDef table; 0 when not listed.
uint16_t class_of(Bytes def, uint16_t glyph) {
  const uint16_t format = def.u16(0);
  if (format == 1) {
    const uint16_t start = def.u16(2), count = def.u16(4);
    if (glyph >= start && glyph < start + count) return def.u16(6 + 2 * static_cast<size_t>(glyph - start));
    return 0;
  }
  if (format == 2) {
    int lo = 0, hi = def.u16(2) - 1;
    while (lo <= hi) {
      const int mid = (lo + hi) / 2;
      const size_t rec = 4 + 6 * static_cast<size_t>(mid);
      if (glyph < def.u16(rec)) hi = mid - 1;
      else if (glyph > def.u16(rec + 2)) lo = mid + 1;
      else return def.u16(rec + 4);
    }
  }
  return 0;
}

}  // namespace

uint8_t Bytes::u8(size_t off) const {
  check(off, 1);
  return data_[off];
}

uint16_t Bytes::u16(size_t off) const {
  check(off, 2);
  return static_cast<uint16_t>(data_[off] << 8 | data_[off + 1]);
}

uint32_t Bytes::u32(size_t off) const {
  check(off, 4);
  return uint32_t{data_[off]} << 24 | uint32_t{data_[off + 1]} << 16 | uint32_t{data_[off + 2]} << 8 |
         uint32_t{data_[off + 3]};
}

Bytes Bytes::sub(size_t off, size_t len) const {
  check(off, len);
  return Bytes(data_.subspan(off, len));
}

Bytes Bytes::from(size_t off) const {
  check(off, 0);
  return Bytes(data_.subspan(off));
}

void Bytes::check(size_t off, size_t len) const {
  if (off > data_.size() || len > data_.size() - off) throw FontError("font data is truncated or damaged");
}

Font::Font(std::span<const uint8_t> data) : bytes_(data) { parse(); }

Font::Font(std::vector<uint8_t> data) : owned_(std::move(data)), bytes_(owned_) { parse(); }

void Font::parse() {
  const uint32_t version = bytes_.u32(0);
  if (version != 0x00010000 && version != 0x74727565) {  // 1.0, or 'true' (Apple)
    throw FontError("not a TrueType font (CFF-flavoured .otf fonts are not supported)");
  }
  const uint16_t count = bytes_.u16(4);
  for (uint16_t i = 0; i < count; ++i) {
    const size_t rec = 12 + 16 * static_cast<size_t>(i);
    const Bytes tag = bytes_.sub(rec, 4);
    std::string name(4, ' ');
    for (size_t c = 0; c < 4; ++c) name[c] = static_cast<char>(tag.u8(c));
    const uint32_t off = bytes_.u32(rec + 8), len = bytes_.u32(rec + 12);
    bytes_.sub(off, len);  // validates the range once, so table() can trust it
    tables_[name] = {off, len};
  }

  const Bytes head = table("head", true);
  units_per_em_ = head.u16(18);
  long_loca_ = head.i16(50) != 0;
  if (units_per_em_ < kMinUnitsPerEm) throw FontError("font has an invalid unitsPerEm");
  glyph_count_ = table("maxp", true).u16(4);
  table("glyf", true);
  table("loca", true);
  table("hmtx", true);
  // The tables read for every glyph: their offsets, so drawing never looks a name up.
  glyf_offset_ = tables_.at("glyf").first;
  loca_offset_ = tables_.at("loca").first;
  hmtx_offset_ = tables_.at("hmtx").first;

  const Bytes hhea = table("hhea", true);
  metrics_.ascender = hhea.i16(4);
  metrics_.descender = hhea.i16(6);
  metrics_.line_gap = hhea.i16(8);
  h_metric_count_ = hhea.u16(34);
  if (h_metric_count_ == 0) throw FontError("font has no horizontal metrics");

  const Bytes os2 = table("OS/2", false);
  if (os2.size() >= kOs2V2Size && os2.u16(0) >= 2) {
    metrics_.x_height = os2.i16(86);
    metrics_.cap_height = os2.i16(88);
  }
  if (metrics_.x_height <= 0) metrics_.x_height = units_per_em_ / 2;
  if (metrics_.cap_height <= 0) metrics_.cap_height = units_per_em_ * 7 / 10;

  read_cmap();
  read_kerning_lookups();
}

Bytes Font::table(const char* tag, bool required) const {
  const auto it = tables_.find(tag);
  if (it == tables_.end()) {
    if (required) throw FontError(std::string("font has no '") + tag + "' table");
    return {};
  }
  return bytes_.sub(it->second.first, it->second.second);
}

void Font::read_cmap() {
  const auto it = tables_.find("cmap");
  if (it == tables_.end()) throw FontError("font has no 'cmap' table");
  const uint32_t base = it->second.first;
  const Bytes cmap = table("cmap", true);
  // Prefer full Unicode (format 12) over the Basic Multilingual Plane (format 4).
  int best_rank = 0;
  for (uint16_t i = 0; i < cmap.u16(2); ++i) {
    const size_t rec = 4 + 8 * static_cast<size_t>(i);
    const uint16_t platform = cmap.u16(rec), encoding = cmap.u16(rec + 2);
    const uint32_t off = cmap.u32(rec + 4);
    const uint16_t format = cmap.u16(off);
    const bool unicode = platform == 0 || (platform == 3 && (encoding == 1 || encoding == 10));
    if (!unicode) continue;
    const int rank = format == 12 ? 2 : format == 4 ? 1 : 0;
    if (rank > best_rank) {
      best_rank = rank;
      cmap_offset_ = base + off;
      cmap_format_ = format;
    }
  }
  if (best_rank == 0) throw FontError("font has no Unicode character map (format 4 or 12)");
}

uint16_t Font::glyph_index(char32_t cp) const {
  try {
    const Bytes sub = bytes_.from(cmap_offset_);
    if (cmap_format_ == 12) {
      int lo = 0, hi = static_cast<int>(sub.u32(12)) - 1;
      while (lo <= hi) {
        const int mid = lo + (hi - lo) / 2;
        const size_t group = 16 + 12 * static_cast<size_t>(mid);
        if (cp < sub.u32(group)) hi = mid - 1;
        else if (cp > sub.u32(group + 4)) lo = mid + 1;
        else return static_cast<uint16_t>(sub.u32(group + 8) + (cp - sub.u32(group)));
      }
      return 0;
    }
    if (cp > 0xFFFF) return 0;
    const uint16_t seg_count = sub.u16(6) / 2;
    if (seg_count == 0) return 0;
    const size_t ends = 14, starts = ends + 2 * size_t{seg_count} + 2;
    const size_t deltas = starts + 2 * size_t{seg_count}, ranges = deltas + 2 * size_t{seg_count};
    int lo = 0, hi = seg_count - 1;
    while (lo < hi) {  // the first segment whose end code is >= cp
      const int mid = (lo + hi) / 2;
      if (sub.u16(ends + 2 * static_cast<size_t>(mid)) < cp) lo = mid + 1;
      else hi = mid;
    }
    const size_t seg = static_cast<size_t>(lo);
    if (sub.u16(starts + 2 * seg) > cp || sub.u16(ends + 2 * seg) < cp) return 0;
    const uint16_t delta = sub.u16(deltas + 2 * seg), range = sub.u16(ranges + 2 * seg);
    if (range == 0) return static_cast<uint16_t>(cp + delta);
    const uint16_t g = sub.u16(ranges + 2 * seg + range + 2 * (cp - sub.u16(starts + 2 * seg)));
    return g == 0 ? 0 : static_cast<uint16_t>(g + delta);
  } catch (const FontError&) {
    return 0;  // a damaged map: the character shows as missing rather than crashing the app
  }
}

int Font::advance(uint16_t glyph) const {
  try {
    const uint16_t index = std::min<uint16_t>(glyph, static_cast<uint16_t>(h_metric_count_ - 1));
    return bytes_.from(hmtx_offset_).u16(4 * size_t{index});
  } catch (const FontError&) {
    return 0;
  }
}

void Font::read_kerning_lookups() {
  const auto it = tables_.find("GPOS");
  if (it == tables_.end()) return;
  try {
    const uint32_t gpos = it->second.first;
    const Bytes g = bytes_.from(gpos);
    const uint32_t features = gpos + g.u16(6), lookups = gpos + g.u16(8);
    const Bytes feature_list = bytes_.from(features), lookup_list = bytes_.from(lookups);

    std::vector<uint16_t> indices;
    for (uint16_t i = 0; i < feature_list.u16(0); ++i) {
      const size_t rec = 2 + 6 * static_cast<size_t>(i);
      const Bytes tag = feature_list.sub(rec, 4);
      if (tag.u8(0) != 'k' || tag.u8(1) != 'e' || tag.u8(2) != 'r' || tag.u8(3) != 'n') continue;
      const Bytes feature = feature_list.from(feature_list.u16(rec + 4));
      for (uint16_t k = 0; k < feature.u16(2); ++k) indices.push_back(feature.u16(4 + 2 * size_t{k}));
    }
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

    for (uint16_t index : indices) {
      if (index >= lookup_list.u16(0)) continue;
      const uint32_t lookup = lookups + lookup_list.u16(2 + 2 * size_t{index});
      const Bytes l = bytes_.from(lookup);
      const uint16_t type = l.u16(0);
      for (uint16_t s = 0; s < l.u16(4); ++s) {
        uint32_t sub = lookup + l.u16(6 + 2 * size_t{s});
        if (type == kLookupExtension) {
          const Bytes ext = bytes_.from(sub);
          if (ext.u16(2) != kLookupPairAdjustment) continue;
          sub += ext.u32(4);
        } else if (type != kLookupPairAdjustment) {
          continue;
        }
        pair_subtables_.push_back(sub);
      }
    }
  } catch (const FontError&) {
    pair_subtables_.clear();  // damaged kerning data: text still renders, unkerned
  }
}

int Font::pair_adjustment(uint32_t subtable, uint16_t left, uint16_t right, bool& applied) const {
  const Bytes p = bytes_.from(subtable);
  const uint16_t format = p.u16(0);
  const int cov = coverage_index(p.from(p.u16(2)), left);
  if (cov < 0) return 0;
  const uint16_t vf1 = p.u16(4), vf2 = p.u16(6);
  const int x_adv = x_advance_offset(vf1);
  const size_t record = static_cast<size_t>(value_record_size(vf1) + value_record_size(vf2));
  if (format == 1) {
    if (cov >= p.u16(8)) return 0;
    const Bytes set = p.from(p.u16(10 + 2 * static_cast<size_t>(cov)));
    int lo = 0, hi = set.u16(0) - 1;
    while (lo <= hi) {
      const int mid = (lo + hi) / 2;
      const size_t rec = 2 + (2 + record) * static_cast<size_t>(mid);
      const uint16_t second = set.u16(rec);
      if (second == right) {
        applied = true;
        return x_adv < 0 ? 0 : set.i16(rec + 2 + static_cast<size_t>(x_adv));
      }
      if (second < right) lo = mid + 1;
      else hi = mid - 1;
    }
    return 0;
  }
  if (format == 2) {
    const uint16_t c1 = class_of(p.from(p.u16(8)), left), c2 = class_of(p.from(p.u16(10)), right);
    const uint16_t class1_count = p.u16(12), class2_count = p.u16(14);
    if (c1 >= class1_count || c2 >= class2_count) return 0;
    applied = true;  // a covered first glyph consumes the lookup, whatever its value
    if (x_adv < 0) return 0;
    const size_t rec = 16 + (size_t{c1} * class2_count + c2) * record;
    return p.i16(rec + static_cast<size_t>(x_adv));
  }
  return 0;
}

int Font::kerning(uint16_t left, uint16_t right) const {
  if (pair_subtables_.empty()) return 0;
  const uint32_t key = uint32_t{left} << 16 | right;
  if (const auto it = kerning_cache_.find(key); it != kerning_cache_.end()) return it->second;
  int total = 0;
  try {
    bool applied = false;
    for (uint32_t sub : pair_subtables_) {
      total += pair_adjustment(sub, left, right, applied);
      if (applied) break;  // the first subtable that applies ends the lookup
    }
  } catch (const FontError&) {
    total = 0;  // damaged kerning data: this pair renders unkerned
  }
  kerning_cache_[key] = static_cast<int16_t>(total);
  return total;
}

void Font::outline(uint16_t glyph, Path& out) const {
  out.clear();
  try {
    append_outline(out, glyph, Transform{}, 0);
  } catch (const FontError&) {
    out.clear();  // a damaged glyph draws as nothing rather than as garbage
  }
}

void Font::append_outline(Path& path, uint16_t glyph, const Transform& t, int depth) const {
  if (glyph >= glyph_count_) return;
  if (depth > kMaxCompositeDepth) throw FontError("composite glyphs nest too deep");
  const Bytes loca = bytes_.from(loca_offset_);
  const uint32_t start = long_loca_ ? loca.u32(4 * size_t{glyph}) : 2u * loca.u16(2 * size_t{glyph});
  const uint32_t end = long_loca_ ? loca.u32(4 * size_t{glyph} + 4) : 2u * loca.u16(2 * size_t{glyph} + 2);
  if (end <= start) return;  // an empty glyph, such as a space
  const Bytes g = bytes_.from(glyf_offset_).sub(start, end - start);
  const int16_t contours = g.i16(0);
  if (contours >= 0) append_simple(path, g, contours, t);
  else append_composite(path, g, t, depth);
}

void Font::append_simple(Path& path, Bytes g, int contours, const Transform& t) const {
  if (contours == 0) return;
  // Reused per thread: outlines are read when a glyph first enters the cache, and the cache
  // then serves it, so this does not allocate once glyphs are warm.
  thread_local std::vector<uint16_t> ends;
  thread_local std::vector<uint8_t> flags;
  thread_local std::vector<Point> pts;
  ends.resize(static_cast<size_t>(contours));
  for (size_t i = 0; i < ends.size(); ++i) ends[i] = g.u16(10 + 2 * i);
  const size_t point_count = size_t{ends.back()} + 1;
  size_t pos = 10 + 2 * static_cast<size_t>(contours);
  pos += 2 + g.u16(pos);  // skip the hinting instructions

  flags.resize(point_count);
  for (size_t i = 0; i < point_count;) {
    const uint8_t f = g.u8(pos++);
    flags[i++] = f;
    if (f & kRepeat) {
      for (uint8_t r = g.u8(pos++); r > 0 && i < point_count; --r) flags[i++] = f;
    }
  }
  pts.resize(point_count);
  int v = 0;
  for (size_t i = 0; i < point_count; ++i) {
    if (flags[i] & kXShort) {
      const int d = g.u8(pos++);
      v += (flags[i] & kXSameOrPositive) ? d : -d;
    } else if (!(flags[i] & kXSameOrPositive)) {
      v += g.i16(pos);
      pos += 2;
    }
    pts[i].x = static_cast<float>(v);
  }
  v = 0;
  for (size_t i = 0; i < point_count; ++i) {
    if (flags[i] & kYShort) {
      const int d = g.u8(pos++);
      v += (flags[i] & kYSameOrPositive) ? d : -d;
    } else if (!(flags[i] & kYSameOrPositive)) {
      v += g.i16(pos);
      pos += 2;
    }
    pts[i].y = static_cast<float>(v);
  }

  // TrueType contours are quadratic splines in which two off-curve points in a row imply an
  // on-curve point halfway between them.
  size_t first = 0;
  for (uint16_t end : ends) {
    const size_t last = end;
    if (last < first || last >= point_count) throw FontError("glyph contour is out of range");
    const size_t n = last - first + 1;
    auto at = [&](size_t k) { return t.apply(pts[first + k % n]); };
    auto on = [&](size_t k) { return (flags[first + k % n] & kOnCurve) != 0; };
    auto mid = [](Point a, Point b) { return Point{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f}; };

    size_t s = 0;
    while (s < n && !on(s)) ++s;
    const Point start = s < n ? at(s) : mid(at(0), at(1));  // all off-curve: start between the first two
    if (s == n) s = 0;
    path.move_to(start);
    Point ctrl{};
    bool have_ctrl = false;
    for (size_t k = 1; k <= n; ++k) {
      const size_t idx = s + k;
      const Point p = at(idx);
      if (on(idx)) {
        if (have_ctrl) path.quad_to(ctrl, p);
        else path.line_to(p);
        have_ctrl = false;
      } else {
        if (have_ctrl) path.quad_to(ctrl, mid(ctrl, p));
        ctrl = p;
        have_ctrl = true;
      }
    }
    if (have_ctrl) path.quad_to(ctrl, start);
    first = last + 1;
  }
}

void Font::append_composite(Path& path, Bytes g, const Transform& t, int depth) const {
  size_t pos = 10;
  uint16_t flags = 0;
  do {
    flags = g.u16(pos);
    const uint16_t component = g.u16(pos + 2);
    pos += 4;
    float dx = 0, dy = 0;
    if (flags & kArgsAreWords) {
      dx = g.i16(pos);
      dy = g.i16(pos + 2);
      pos += 4;
    } else {
      dx = static_cast<int8_t>(g.u8(pos));
      dy = static_cast<int8_t>(g.u8(pos + 1));
      pos += 2;
    }
    if (!(flags & kArgsAreXY)) dx = dy = 0;  // point matching: rare, and unused by UI fonts
    float sx = 1, sy = 1;
    if (flags & kHaveScale) {
      sx = sy = g.i16(pos) * kF2Dot14;
      pos += 2;
    } else if (flags & kHaveXYScale) {
      sx = g.i16(pos) * kF2Dot14;
      sy = g.i16(pos + 2) * kF2Dot14;
      pos += 4;
    } else if (flags & kHaveTwoByTwo) {
      sx = g.i16(pos) * kF2Dot14;
      sy = g.i16(pos + 6) * kF2Dot14;
      pos += 8;
    }
    // UI fonts offset and scale components; a skewed component (the off-diagonal terms of
    // the two-by-two) is drawn without its skew.
    const Transform local{t.sx * sx, t.sy * sy, t.sx * dx + t.tx, t.sy * dy + t.ty};
    append_outline(path, component, local, depth + 1);
  } while (flags & kMoreComponents);
}

}  // namespace smalti::detail
