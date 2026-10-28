// Containers that place their children: Column, Row, Grid, Card, and Spacer to push things apart.
#pragma once

#include <vector>

#include "smalti/widget.hpp"

namespace smalti {

struct StackOptions {
  float spacing = 0;
  Insets padding{};
  Align align = Align::stretch;  // default placement of children across the stack
  Align justify = Align::start;  // where children sit along the stack when none stretches
};

// Children top to bottom. A child's stretch() shares out leftover height.
class Column : public Widget {
 public:
  explicit Column(StackOptions options = {}) : options_(options) {}
  Size measure(Size available) override;
  void arrange(Rect bounds) override;
  bool accepts_children() const override { return true; }
  StackOptions& options() { return options_; }

 protected:
  StackOptions options_;
};

// Children left to right. A child's stretch() shares out leftover width.
class Row : public Widget {
 public:
  explicit Row(StackOptions options = {}) : options_(options) {}
  Size measure(Size available) override;
  void arrange(Rect bounds) override;
  bool accepts_children() const override { return true; }
  StackOptions& options() { return options_; }

 protected:
  StackOptions options_;
};

// Empty space that takes a share of leftover room (stretch 1 by default).
class Spacer final : public Widget {
 public:
  explicit Spacer(float stretch = 1, float size = 0);
  Size measure(Size available) override;

 private:
  float size_;
};

// A column: fixed width, as wide as its widest child, or a share of the leftover width.
struct GridTrack {
  enum class Kind : uint8_t { fixed, fit, stretch };
  Kind kind = Kind::fit;
  float value = 1;  // pixels for fixed, weight for stretch
  static GridTrack fixed(float px) { return {Kind::fixed, px}; }
  static GridTrack fit() { return {Kind::fit, 0}; }
  static GridTrack stretch(float weight = 1) { return {Kind::stretch, weight}; }
};

struct GridOptions {
  std::vector<GridTrack> columns{GridTrack::fit(), GridTrack::stretch()};
  float column_spacing = 12;
  float row_spacing = 8;
  Insets padding{};
};

// Children fill the cells left to right, then down; set_column_span() widens one.
class Grid : public Widget {
 public:
  explicit Grid(GridOptions options = {}) : options_(std::move(options)) {}
  Size measure(Size available) override;
  void arrange(Rect bounds) override;
  bool accepts_children() const override { return true; }

 private:
  struct Cell {
    Widget* widget;
    int row, column, span;
  };
  std::vector<Cell> place() const;
  std::vector<float> column_widths(const std::vector<Cell>& cells, float width) const;
  GridOptions options_;
};

struct CardOptions {
  Insets padding = Insets::all(20);
  float spacing = 12;
};

// A raised surface whose children stack top to bottom: a soft shadow where the theme has one, a
// hairline border and a top highlight where it does not.
class Card : public Column {
 public:
  explicit Card(CardOptions options = {})
      : Column(StackOptions{options.spacing, options.padding, Align::stretch, Align::start}) {}
  void paint(Painter& p) override;
  float overflow() const override { return 44; }  // the soft shadow below the card
};

}  // namespace smalti
