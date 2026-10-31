// The column widths of a grid with spanning children; see grid_columns.hpp.
//
// The problem: growing widths x (each at least its own least width) and sharing columns that
// split L = room - fixed - sum(x) by weight. A span fits when the growing columns it covers,
// plus its share of L, reach what it needs beyond the fixed columns it covers. With T = sum(x),
// let H(T) be the least total of the growing columns when the sharing columns split
// room - fixed - T. H is the value of a linear program whose right-hand side rises linearly
// with T, so it is convex and piecewise linear, and a split that fits with total T exists
// exactly when H(T) <= T. The narrowest fit is the first such T. Newton steps from the least
// total reach it exactly: the tangent of a convex function never passes its first root, and
// each step crosses at least one kink of H. (Exactly at a kink the slope may be the one before
// it, which is steeper; that step falls short and the next one goes on.)
//
// H(T) itself comes from the greedy that is exact for covering intervals: spans in order of
// their right end, each one's shortfall put where it helps every later span that overlaps it.
#include "core/grid_columns.hpp"

#include <algorithm>
#include <optional>

namespace smalti::detail {

namespace {

// A span short by less than this fits.
constexpr double kFitSlack = 0.01;
// Newton steps before giving up. Each crosses a kink of H; real grids have a few.
constexpr int kMaxSteps = 64;
// A span whose share is this close to 1 covers every sharing column.
constexpr double kWholeShare = 1.0 - 1e-9;

// A width and its rate of change as the growing columns' total rises.
struct Linear {
  double at = 0, slope = 0;
};

// A span in the growing columns' own numbering.
struct Demand {
  size_t first, last;  // the growing columns it covers: [first, last)
  size_t from;         // where its growth goes: [from, last)
  double need;         // what it needs from them and its share
  double share;        // the part of the shared width it covers
};

// Puts the demands in the order the greedy takes them, by right end and the narrower first of
// those ending together, and chooses where each one's growth goes. Every later span that
// overlaps a span covers its last growing column, so growth there helps them all; it helps them
// just as much anywhere they all cover, so it is spread over that part, [from, last).
void order(std::vector<Demand>& demands, size_t growing) {
  std::stable_sort(demands.begin(), demands.end(), [](const Demand& a, const Demand& b) {
    return a.last != b.last ? a.last < b.last : a.first > b.first;
  });
  std::vector<bool> later_starts(growing, false);
  for (size_t j = demands.size(); j-- > 0;) {
    Demand& d = demands[j];
    d.from = d.first;
    for (size_t g = d.last - 1; g > d.first; --g) {
      if (later_starts[g]) {
        d.from = g;
        break;
      }
    }
    later_starts[d.first] = true;
  }
}

// The least growing widths that satisfy the demands when the sharing columns split `left`,
// each with the rate it changes at as the total rises (and `left` falls by as much).
std::vector<Linear> fill(std::span<const double> least, std::span<const Demand> demands,
                         double left) {
  std::vector<Linear> x(least.size());
  for (size_t g = 0; g < least.size(); ++g) x[g].at = least[g];
  for (const Demand& d : demands) {
    Linear have{d.share * left, -d.share};
    for (size_t g = d.first; g < d.last; ++g) {
      have.at += x[g].at;
      have.slope += x[g].slope;
    }
    const Linear short_by{d.need - have.at, -have.slope};
    if (short_by.at <= 0) continue;
    const auto count = static_cast<double>(d.last - d.from);
    for (size_t g = d.from; g < d.last; ++g) {
      x[g].at += std::max(0.0, short_by.at) / count;
      x[g].slope += short_by.slope / count;
    }
  }
  return x;
}

// The growing widths with the least total that lets every demand fit, the growing and sharing
// columns splitting `room`; nothing if no split does.
std::optional<std::vector<Linear>> narrowest_fit(std::span<const double> least,
                                                 std::span<const Demand> demands, double room) {
  double total = 0;
  for (const double w : least) total += w;
  for (int step = 0; step < kMaxSteps && total <= room + kFitSlack; ++step) {
    std::vector<Linear> x = fill(least, demands, std::max(0.0, room - total));
    Linear excess{-total, -1};  // what the demands take beyond `total`
    for (const Linear& w : x) {
      excess.at += w.at;
      excess.slope += w.slope;
    }
    if (excess.at <= kFitSlack) return x;
    if (excess.slope >= 0) return std::nullopt;  // growing further loses as much as it gains
    total -= excess.at / excess.slope;
  }
  return std::nullopt;
}

}  // namespace

std::vector<float> solve_columns(std::span<const ColumnRule> columns,
                                 std::span<const SpanNeed> spans, float room) {
  const size_t n = columns.size();
  std::vector<size_t> growing_before(n + 1, 0);  // growing columns left of column c
  std::vector<double> least;
  double fixed = 0, weights = 0;
  for (size_t c = 0; c < n; ++c) {
    growing_before[c + 1] = growing_before[c];
    switch (columns[c].kind) {
      case ColumnRule::Kind::fixed:
        fixed += columns[c].value;
        break;
      case ColumnRule::Kind::grows:
        least.push_back(columns[c].value);
        ++growing_before[c + 1];
        break;
      case ColumnRule::Kind::shares:
        weights += columns[c].value;
        break;
    }
  }
  // A span over no growing column is left out: only a narrower total widens it, and the solve
  // seeks the narrowest anyway.
  std::vector<Demand> demands;
  for (const SpanNeed& s : spans) {
    const size_t first = growing_before[s.first], last = growing_before[s.last];
    if (first == last) continue;
    double need = s.need, weight = 0;
    for (size_t c = s.first; c < s.last; ++c) {
      if (columns[c].kind == ColumnRule::Kind::fixed) need -= columns[c].value;
      if (columns[c].kind == ColumnRule::Kind::shares) weight += columns[c].value;
    }
    demands.push_back({first, last, first, need, weights > 0 ? weight / weights : 0.0});
  }
  order(demands, least.size());

  const double share_room = static_cast<double>(room) - fixed;
  std::optional<std::vector<Linear>> x = narrowest_fit(least, demands, share_room);
  if (!x) {
    // No split fits. A span over every sharing column gains nothing from growth until the
    // sharing columns are gone, so it keeps what sharing gives it; the rest are made to fit.
    std::erase_if(demands, [](const Demand& d) { return d.share >= kWholeShare; });
    order(demands, least.size());
    x = narrowest_fit(least, demands, share_room);
    if (!x) x = fill(least, demands, 0.0);  // overflowing: the sharing columns get nothing
  }

  double total = 0;
  for (const Linear& w : *x) total += w.at;
  const double left = std::max(0.0, share_room - total);
  std::vector<float> widths(n, 0.0f);
  for (size_t c = 0; c < n; ++c) {
    switch (columns[c].kind) {
      case ColumnRule::Kind::fixed:
        widths[c] = columns[c].value;
        break;
      case ColumnRule::Kind::grows:
        widths[c] = static_cast<float>((*x)[growing_before[c]].at);
        break;
      case ColumnRule::Kind::shares:
        if (weights > 0) widths[c] = static_cast<float>(left * columns[c].value / weights);
        break;
    }
  }
  return widths;
}

}  // namespace smalti::detail
