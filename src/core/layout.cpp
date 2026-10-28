// Column, Row, Spacer, Grid and Card: measure children, then share out the space.
#include "smalti/layout.hpp"

#include <algorithm>
#include <limits>

#include "core/state.hpp"

namespace smalti {

namespace {

constexpr float kUnbounded = 1e9f;

// The shared logic of Column and Row, written once along a "main" axis.
struct Axis {
  bool vertical;
  float main(Size s) const { return vertical ? s.height : s.width; }
  float cross(Size s) const { return vertical ? s.width : s.height; }
  Size make(float main_v, float cross_v) const { return vertical ? Size{cross_v, main_v} : Size{main_v, cross_v}; }
  Rect rect(float main_pos, float cross_pos, float main_len, float cross_len) const {
    return vertical ? Rect{cross_pos, main_pos, cross_len, main_len} : Rect{main_pos, cross_pos, main_len, cross_len};
  }
};

Size max_size(Size a, Size b) { return {std::max(a.width, b.width), std::max(a.height, b.height)}; }

Size measure_stack(Widget& self, const StackOptions& o, Size available, Axis axis) {
  const Size inner{std::max(0.0f, available.width - o.padding.horizontal()),
                   std::max(0.0f, available.height - o.padding.vertical())};
  float main_total = 0, cross_max = 0;
  int count = 0;
  // By index, holding the widget rather than a reference into the vector: a measure() that adds
  // a sibling reallocates it.
  for (size_t i = 0; i < self.children().size(); ++i) {
    Widget* child = self.children()[i].get();
    if (!child->visible()) continue;
    const Size s = max_size(child->measure(axis.make(kUnbounded, axis.cross(inner))), child->min_size());
    main_total += axis.main(s);
    cross_max = std::max(cross_max, axis.cross(s));
    ++count;
  }
  if (count > 1) main_total += o.spacing * static_cast<float>(count - 1);
  const Size content = axis.make(main_total, cross_max);
  return max_size({content.width + o.padding.horizontal(), content.height + o.padding.vertical()}, self.min_size());
}

void arrange_stack(Widget& self, const StackOptions& o, Rect bounds, Axis axis) {
  const Rect inner = bounds.inset(o.padding);
  const float inner_main = axis.vertical ? inner.height : inner.width;
  const float inner_cross = axis.vertical ? inner.width : inner.height;
  const float main_start = axis.vertical ? inner.y : inner.x;
  const float cross_start = axis.vertical ? inner.x : inner.y;

  float used = 0, total_stretch = 0;
  int count = 0;
  for (size_t i = 0; i < self.children().size(); ++i) {
    Widget* child = self.children()[i].get();
    if (!child->visible()) continue;
    used += axis.main(max_size(child->measure(axis.make(kUnbounded, inner_cross)), child->min_size()));
    total_stretch += child->stretch();
    ++count;
  }
  if (count > 1) used += o.spacing * static_cast<float>(count - 1);
  const float extra = std::max(0.0f, inner_main - used);
  float pos = main_start;
  if (total_stretch == 0) {
    if (o.justify == Align::center) pos += extra * 0.5f;
    else if (o.justify == Align::end) pos += extra;
  }
  for (size_t i = 0; i < self.children().size(); ++i) {
    Widget* child = self.children()[i].get();
    if (!child->visible()) continue;
    const Size s = max_size(child->measure(axis.make(kUnbounded, inner_cross)), child->min_size());
    float main_len = axis.main(s);
    if (total_stretch > 0 && child->stretch() > 0) main_len += extra * child->stretch() / total_stretch;
    float cross_len = std::min(axis.cross(s), inner_cross);
    float cross_pos = cross_start;
    switch (child->align_or(o.align)) {
      case Align::stretch: cross_len = inner_cross; break;
      case Align::center: cross_pos += (inner_cross - cross_len) * 0.5f; break;
      case Align::end: cross_pos += inner_cross - cross_len; break;
      case Align::start: break;
    }
    child->arrange(axis.rect(pos, cross_pos, main_len, cross_len));
    pos += main_len + o.spacing;
  }
}

}  // namespace

Size Column::measure(Size available) { return measure_stack(*this, options_, available, {true}); }

void Column::arrange(Rect bounds) {
  Widget::arrange(bounds);
  arrange_stack(*this, options_, bounds, {true});
}

Size Row::measure(Size available) { return measure_stack(*this, options_, available, {false}); }

void Row::arrange(Rect bounds) {
  Widget::arrange(bounds);
  arrange_stack(*this, options_, bounds, {false});
}

Spacer::Spacer(float stretch, float size) : size_(size) { set_stretch(stretch); }

Size Spacer::measure(Size) { return {size_, size_}; }

Grid::Grid(GridOptions options) : options_(std::move(options)) {
  if (options_.columns.empty())
    options_.columns.push_back(GridTrack::stretch());  // one column that fills
}

std::vector<Grid::Cell> Grid::place() const {
  std::vector<Cell> cells;
  const int columns = std::max<int>(1, static_cast<int>(options_.columns.size()));
  int row = 0, col = 0;
  for (const auto& child : children()) {
    if (!child->visible()) continue;
    const int span = std::min(child->column_span(), columns);
    if (col + span > columns) {
      ++row;
      col = 0;
    }
    cells.push_back({child.get(), row, col, span});
    col += span;
  }
  return cells;
}

std::vector<float> Grid::column_widths(const std::vector<Cell>& cells, float width) const {
  const size_t n = std::max<size_t>(1, options_.columns.size());
  std::vector<float> widths(n, 0.0f);
  float fixed_total = 0, weight_total = 0;
  for (size_t c = 0; c < options_.columns.size(); ++c) {
    const GridTrack& t = options_.columns[c];
    if (t.kind == GridTrack::Kind::fixed) widths[c] = t.value;
    if (t.kind == GridTrack::Kind::stretch) weight_total += t.value;
  }
  // Fit columns (and stretch columns when there is no width to share) take their widest child.
  const bool share = width < kUnbounded * 0.5f && weight_total > 0;
  const auto content_sized = [&](size_t c) {
    const GridTrack::Kind k = options_.columns[c].kind;
    return k == GridTrack::Kind::fit || (k == GridTrack::Kind::stretch && !share);
  };
  for (const Cell& cell : cells) {
    if (cell.span != 1) continue;
    const size_t c = static_cast<size_t>(cell.column);
    if (content_sized(c)) {
      const Size s = max_size(cell.widget->measure({kUnbounded, kUnbounded}), cell.widget->min_size());
      widths[c] = std::max(widths[c], s.width);
    }
  }
  // A spanning child wider than the columns it covers widens the content-sized ones among them,
  // evenly. Fixed columns keep their width; shared stretch columns get theirs below.
  for (const Cell& cell : cells) {
    if (cell.span == 1) continue;
    const size_t first = static_cast<size_t>(cell.column),
                 last = first + static_cast<size_t>(cell.span);
    float have = options_.column_spacing * static_cast<float>(cell.span - 1);
    int growable = 0;
    for (size_t c = first; c < last; ++c) {
      have += widths[c];
      if (content_sized(c)) ++growable;
    }
    const float need =
        max_size(cell.widget->measure({kUnbounded, kUnbounded}), cell.widget->min_size()).width;
    // A span over a column that stretches is left to it: the shared-out width lands there, and
    // charging the fit columns instead would squeeze the stretch column to nothing.
    bool stretches = false;
    for (size_t c = first; c < last; ++c)
      stretches |= share && options_.columns[c].kind == GridTrack::Kind::stretch;
    if (stretches || need <= have || growable == 0) continue;
    const float extra = (need - have) / static_cast<float>(growable);
    for (size_t c = first; c < last; ++c) {
      if (content_sized(c)) widths[c] += extra;
    }
  }
  for (size_t c = 0; c < options_.columns.size(); ++c) {
    if (options_.columns[c].kind != GridTrack::Kind::stretch || share) fixed_total += widths[c];
  }
  if (share) {
    const float spacing = options_.column_spacing * static_cast<float>(n - 1);
    const float left = std::max(0.0f, width - fixed_total - spacing);
    for (size_t c = 0; c < options_.columns.size(); ++c) {
      if (options_.columns[c].kind == GridTrack::Kind::stretch) widths[c] = left * options_.columns[c].value / weight_total;
    }
  }
  return widths;
}

Size Grid::measure(Size available) {
  const std::vector<Cell> cells = place();
  const float inner_w = available.width - options_.padding.horizontal();
  const std::vector<float> widths = column_widths(cells, inner_w);
  float total_w = 0;
  for (float w : widths) total_w += w;
  total_w += options_.column_spacing * static_cast<float>(widths.size() - 1);
  float total_h = 0, row_h = 0;
  int row = 0;
  for (const Cell& cell : cells) {
    if (cell.row != row) {
      total_h += row_h + options_.row_spacing;
      row_h = 0;
      row = cell.row;
    }
    float w = options_.column_spacing * static_cast<float>(cell.span - 1);
    for (int c = cell.column; c < cell.column + cell.span; ++c) w += widths[static_cast<size_t>(c)];
    row_h = std::max(row_h, max_size(cell.widget->measure({w, kUnbounded}), cell.widget->min_size()).height);
  }
  total_h += row_h;
  return max_size({total_w + options_.padding.horizontal(), total_h + options_.padding.vertical()}, min_size());
}

void Grid::arrange(Rect bounds) {
  Widget::arrange(bounds);
  const Rect inner = bounds.inset(options_.padding);
  const std::vector<Cell> cells = place();
  const std::vector<float> widths = column_widths(cells, inner.width);
  std::vector<float> lefts(widths.size(), inner.x);
  for (size_t c = 1; c < widths.size(); ++c) lefts[c] = lefts[c - 1] + widths[c - 1] + options_.column_spacing;
  // Row heights first, so a row's cells can align within it.
  std::vector<float> heights;
  for (const Cell& cell : cells) {
    if (static_cast<size_t>(cell.row) >= heights.size()) heights.resize(static_cast<size_t>(cell.row) + 1, 0.0f);
    float w = options_.column_spacing * static_cast<float>(cell.span - 1);
    for (int c = cell.column; c < cell.column + cell.span; ++c) w += widths[static_cast<size_t>(c)];
    heights[static_cast<size_t>(cell.row)] = std::max(
        heights[static_cast<size_t>(cell.row)], max_size(cell.widget->measure({w, kUnbounded}), cell.widget->min_size()).height);
  }
  std::vector<float> tops(heights.size(), inner.y);
  for (size_t r = 1; r < heights.size(); ++r) tops[r] = tops[r - 1] + heights[r - 1] + options_.row_spacing;
  for (const Cell& cell : cells) {
    float w = options_.column_spacing * static_cast<float>(cell.span - 1);
    for (int c = cell.column; c < cell.column + cell.span; ++c) w += widths[static_cast<size_t>(c)];
    const float row_h = heights[static_cast<size_t>(cell.row)];
    const Size s = max_size(cell.widget->measure({w, kUnbounded}), cell.widget->min_size());
    // Within a row, cells sit centred vertically unless told otherwise; across, they fill.
    float y = tops[static_cast<size_t>(cell.row)], h = std::min(s.height, row_h);
    switch (cell.widget->align_or(Align::center)) {
      case Align::stretch: h = row_h; break;
      case Align::center: y += (row_h - h) * 0.5f; break;
      case Align::end: y += row_h - h; break;
      case Align::start: break;
    }
    cell.widget->arrange({lefts[static_cast<size_t>(cell.column)], y, w, h});
  }
}

namespace {
constexpr float kCardShadowBlur = 12;
constexpr float kCardShadowOffset = 4;
constexpr float kCardContactBlur = 1.5f;
}  // namespace

void Card::paint(Painter& p) {
  const Theme& th = theme();
  const Rect r = bounds();
  const float radius = th.radius_card;
  if (th.shadows) {
    p.shadow({r.x, r.y + kCardShadowOffset, r.width, r.height}, radius, kCardShadowBlur, th.shadow, r);
    p.shadow({r.x, r.y + 1, r.width, r.height}, radius, kCardContactBlur, th.shadow.with_alpha(th.shadow.a * 0.5f), r);
  }
  p.fill_rounded_rect(r, radius, th.raised);
  if (!th.shadows) p.fill_rect({r.x + radius, r.y + p.hairline(), r.width - 2 * radius, p.hairline()}, th.top_highlight);
  p.stroke_rounded_rect(r, radius, 0, th.border);
}

}  // namespace smalti
