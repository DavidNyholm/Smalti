// The widths of a grid's columns when children span several of them, solved for all spans
// together. Pure logic, tested on its own.
//
// Growing (content-sized) columns take as little as lets every span fit; sharing (stretch)
// columns split what the others leave, by weight. Widening a growing column therefore narrows
// every sharing column, and with it each span that covers one, so the spans are solved as one
// problem rather than one after another.
#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace smalti::detail {

// How a column takes part in the solve.
struct ColumnRule {
  enum class Kind { fixed, grows, shares };
  Kind kind;
  float value;  // fixed: its width; grows: the least it takes; shares: its weight
};

// A child covering columns [first, last), needing `need` across them, spacing between them
// already taken out.
struct SpanNeed {
  size_t first, last;
  float need;
};

// The width of each column, for columns that have `room` between them (spacing taken out).
//
// When some split lets every span fit within `room`, the growing columns take the least total
// that does, so the sharing columns get the most. A lone span's growth is split evenly over its
// growing columns; spans that overlap put more of it in the columns they share. When no split
// fits, spans over every sharing column are left with what sharing gives them (no growth can
// help them without overflowing), and the others are made to fit, overflowing `room` if they
// must.
std::vector<float> solve_columns(std::span<const ColumnRule> columns,
                                 std::span<const SpanNeed> spans, float room);

}  // namespace smalti::detail
