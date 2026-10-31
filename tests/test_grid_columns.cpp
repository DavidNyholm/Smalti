// The grid's column solve on its own: small cases whose answer is known, then random grids
// against a brute-force search over the growing columns' widths.
#include <algorithm>
#include <cstdio>
#include <optional>
#include <random>
#include <vector>

#include "check.hpp"
#include "core/grid_columns.hpp"

using smalti::detail::ColumnRule;
using smalti::detail::solve_columns;
using smalti::detail::SpanNeed;
using Kind = ColumnRule::Kind;

namespace {

constexpr float kSlack = 0.05f;  // the solver's own tolerance, plus float rounding

ColumnRule fixed(float width) { return {Kind::fixed, width}; }
ColumnRule grows(float least = 0) { return {Kind::grows, least}; }
ColumnRule shares(float weight = 1) { return {Kind::shares, weight}; }

float across(const std::vector<float>& widths, const SpanNeed& s) {
  float w = 0;
  for (size_t c = s.first; c < s.last; ++c) w += widths[c];
  return w;
}

float sum(const std::vector<float>& widths) {
  float w = 0;
  for (const float x : widths) w += x;
  return w;
}

}  // namespace

TEST(grid_columns_one_span_grows_its_fit_columns_evenly) {
  const std::vector<ColumnRule> cols{grows(30), grows(0)};
  const std::vector<SpanNeed> spans{{0, 2, 100}};
  const std::vector<float> w = solve_columns(cols, spans, 1e9f);
  CHECK_NEAR(w[0], 65.0f, kSlack);  // 30 + 35
  CHECK_NEAR(w[1], 35.0f, kSlack);
}

TEST(grid_columns_overlapping_spans_grow_the_column_they_share) {
  // Grown one span at a time, evenly, the columns take 150 and overflow 120; growing only the
  // shared middle column, they take 100 and the stretch column keeps 20.
  const std::vector<ColumnRule> cols{grows(), grows(), grows(), shares()};
  const std::vector<SpanNeed> spans{{0, 2, 100}, {1, 3, 100}};
  const std::vector<float> w = solve_columns(cols, spans, 120);
  CHECK_NEAR(w[0], 0.0f, kSlack);
  CHECK_NEAR(w[1], 100.0f, kSlack);
  CHECK_NEAR(w[2], 0.0f, kSlack);
  CHECK_NEAR(w[3], 20.0f, kSlack);
}

TEST(grid_columns_growth_goes_where_a_span_over_the_stretch_column_needs_it) {
  // The first span grows its columns; the second covers the stretch column and only the second
  // of those, so growth in the first column would take width it needs.
  const std::vector<ColumnRule> cols{grows(), grows(), shares()};
  const std::vector<SpanNeed> spans{{0, 2, 50}, {1, 3, 180}};
  const std::vector<float> w = solve_columns(cols, spans, 200);
  CHECK_NEAR(w[0] + w[1], 50.0f, kSlack);
  CHECK(across(w, spans[1]) >= 180 - kSlack);
}

TEST(grid_columns_a_span_over_every_stretch_column_keeps_within_the_width) {
  // No split fits: growing the fit column only narrows the stretch column as much. The fit
  // column keeps its own width and the field beside it its share.
  const std::vector<ColumnRule> cols{grows(20), shares()};
  const std::vector<SpanNeed> spans{{0, 2, 250}};
  const std::vector<float> w = solve_columns(cols, spans, 200);
  CHECK_NEAR(w[0], 20.0f, kSlack);
  CHECK_NEAR(w[1], 180.0f, kSlack);
}

TEST(grid_columns_a_span_over_fit_columns_overflows_rather_than_fall_short) {
  // Fit columns take what their children need, as they do for single children too wide.
  const std::vector<ColumnRule> cols{grows(), grows(), shares(), fixed(10)};
  const std::vector<SpanNeed> spans{{0, 2, 300}};
  const std::vector<float> w = solve_columns(cols, spans, 200);
  CHECK_NEAR(w[0] + w[1], 300.0f, kSlack);
  CHECK_NEAR(w[2], 0.0f, kSlack);
  CHECK_NEAR(w[3], 10.0f, kSlack);
}

TEST(grid_columns_a_span_over_most_of_the_stretch_weight_fits_exactly) {
  // Widening the fit column by 1 gives the span only 0.05, as the stretch column beside it
  // loses 0.95: rounds that only make up each shortfall would take hundreds to settle.
  const std::vector<ColumnRule> cols{grows(), shares(19), shares(1)};
  const std::vector<SpanNeed> spans{{0, 2, 195}};
  const std::vector<float> w = solve_columns(cols, spans, 200);
  CHECK_NEAR(w[0], 100.0f, kSlack);
  CHECK_NEAR(w[1], 95.0f, kSlack);
  CHECK_NEAR(w[2], 5.0f, kSlack);
}

TEST(grid_columns_spans_over_part_of_the_stretch_weight_share_it_out) {
  // Both spans cover the heavier stretch column, which alone gives each of them enough.
  const std::vector<ColumnRule> cols{grows(), shares(3), grows(), shares(1)};
  const std::vector<SpanNeed> spans{{0, 2, 90}, {1, 3, 90}};
  const std::vector<float> w = solve_columns(cols, spans, 120);
  CHECK_NEAR(w[0] + w[2], 0.0f, kSlack);
  CHECK_NEAR(w[1], 90.0f, kSlack);
  CHECK_NEAR(w[3], 30.0f, kSlack);
}

namespace {

// A random grid: a few columns of each kind, spans of two or more columns, and a width.
struct Instance {
  std::vector<ColumnRule> cols;
  std::vector<SpanNeed> spans;
  float room;
};

constexpr int kMaxGrowing = 3;  // keeps the brute force small
constexpr float kLattice = 10;  // the brute force tries growing widths in these steps
constexpr int kLatticeSteps = 20;

Instance random_instance(std::mt19937& rng) {
  Instance in;
  const size_t n = 2 + rng() % 4;
  int growing = 0;
  for (size_t c = 0; c < n; ++c) {
    const unsigned pick = rng() % 10;
    if (pick < 5 && growing < kMaxGrowing) {
      in.cols.push_back(grows(static_cast<float>(rng() % 5) * kLattice));
      ++growing;
    } else if (pick < 8) {
      in.cols.push_back(shares(static_cast<float>(1 + rng() % 3)));
    } else {
      in.cols.push_back(fixed(static_cast<float>(rng() % 5) * kLattice));
    }
  }
  const size_t count = 1 + rng() % 3;
  for (size_t i = 0; i < count; ++i) {
    const size_t first = rng() % (n - 1);
    const size_t last = first + 2 + rng() % (n - first - 1);
    in.spans.push_back({first, last, static_cast<float>(rng() % 21) * kLattice});
  }
  in.room = static_cast<float>(10 + rng() % 21) * kLattice;
  return in;
}

// The widths when the growing columns take `x`, the stretch columns sharing what is left.
std::vector<float> widths_with(const Instance& in, const std::vector<float>& x) {
  float taken = 0, weights = 0;
  for (size_t c = 0, g = 0; c < in.cols.size(); ++c) {
    if (in.cols[c].kind == Kind::fixed) taken += in.cols[c].value;
    if (in.cols[c].kind == Kind::grows) taken += x[g++];
    if (in.cols[c].kind == Kind::shares) weights += in.cols[c].value;
  }
  const float left = std::max(0.0f, in.room - taken);
  std::vector<float> w;
  for (size_t c = 0, g = 0; c < in.cols.size(); ++c) {
    if (in.cols[c].kind == Kind::fixed) w.push_back(in.cols[c].value);
    if (in.cols[c].kind == Kind::grows) w.push_back(x[g++]);
    if (in.cols[c].kind == Kind::shares) w.push_back(left * in.cols[c].value / weights);
  }
  return w;
}

bool all_fit(const Instance& in, const std::vector<float>& w, float slack) {
  for (const SpanNeed& s : in.spans) {
    if (across(w, s) < s.need - slack) return false;
  }
  return true;
}

// The least growing total, over the lattice, with which every span fits within the room.
std::optional<float> brute_force(const Instance& in) {
  std::vector<float> least;
  for (const ColumnRule& r : in.cols) {
    if (r.kind == Kind::grows) least.push_back(r.value);
  }
  std::optional<float> best;
  std::vector<int> step(least.size(), 0);
  for (;;) {
    std::vector<float> x(least.size());
    for (size_t g = 0; g < x.size(); ++g) x[g] = least[g] + static_cast<float>(step[g]) * kLattice;
    const std::vector<float> w = widths_with(in, x);
    if (all_fit(in, w, 0) && sum(w) <= in.room) {
      float total = 0;
      for (const float v : x) total += v;
      if (!best || total < *best) best = total;
    }
    size_t g = 0;
    while (g < step.size() && ++step[g] > kLatticeSteps) step[g++] = 0;
    if (g == step.size()) break;
  }
  return best;
}

float growing_total(const Instance& in, const std::vector<float>& w) {
  float total = 0;
  for (size_t c = 0; c < in.cols.size(); ++c) {
    if (in.cols[c].kind == Kind::grows) total += w[c];
  }
  return total;
}

void print(const Instance& in) {
  std::printf("    room %g, columns:", static_cast<double>(in.room));
  for (const ColumnRule& r : in.cols) {
    const char* kind = r.kind == Kind::fixed ? "fixed" : r.kind == Kind::grows ? "grows" : "shares";
    std::printf(" %s %g", kind, static_cast<double>(r.value));
  }
  std::printf("; spans:");
  for (const SpanNeed& s : in.spans)
    std::printf(" [%zu,%zu) %g", s.first, s.last, static_cast<double>(s.need));
  std::printf("\n");
}

}  // namespace

TEST(grid_columns_random_grids_match_a_brute_force_search) {
  constexpr int kInstances = 400;
  int fitting = 0;
  for (int i = 0; i < kInstances; ++i) {
    std::mt19937 rng(static_cast<unsigned>(i));
    const Instance in = random_instance(rng);
    const std::vector<float> w = solve_columns(in.cols, in.spans, in.room);
    bool ok = w.size() == in.cols.size();
    for (size_t c = 0; ok && c < w.size(); ++c) {
      if (in.cols[c].kind == Kind::grows && w[c] < in.cols[c].value - kSlack) ok = false;
    }
    // When some split fits, the solver finds one at least as narrow as the search's best.
    if (const std::optional<float> best = brute_force(in); ok && best) {
      ++fitting;
      ok = all_fit(in, w, kSlack) && sum(w) <= in.room + kSlack &&
           growing_total(in, w) <= *best + kSlack;
    }
    // Whatever happens, a span over a growing column and not every stretch column fits.
    float weights = 0;
    for (const ColumnRule& r : in.cols) weights += r.kind == Kind::shares ? r.value : 0;
    for (const SpanNeed& s : in.spans) {
      float weight = 0;
      bool grows_any = false;
      for (size_t c = s.first; c < s.last; ++c) {
        if (in.cols[c].kind == Kind::shares) weight += in.cols[c].value;
        if (in.cols[c].kind == Kind::grows) grows_any = true;
      }
      const bool helped = grows_any && (weights == 0 || weight < weights);
      if (ok && helped && across(w, s) < s.need - kSlack) ok = false;
    }
    if (!ok) {
      CHECK(false);
      std::printf("    instance %d\n", i);
      print(in);
      return;
    }
  }
  CHECK(fitting > kInstances / 4);  // the search found enough fitting cases to mean something
}
