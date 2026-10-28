// Column, Row, Spacer, Grid and Card: where children end up.
#include "check.hpp"
#include "support.hpp"

using namespace smalti;

namespace {

// A widget with a fixed preferred size, so layouts can be checked exactly.
class Box : public Widget {
 public:
  Box(int w, int h) : size_{static_cast<float>(w), static_cast<float>(h)} {}
  Size measure(Size) override { return size_; }

 private:
  Size size_;
};

}  // namespace

TEST(layout_column_spacing_padding_and_stretch) {
  App app(headless());
  auto win = app.open_window({"t", {200, 300}});
  auto col = win->add<Column>(StackOptions{10, Insets::all(20), Align::stretch, Align::start});
  auto a = col->add<Box>(50, 30);
  auto b = col->add<Box>(80, 40);
  b->set_stretch(1);
  auto c = col->add<Box>(60, 20);
  testing::Driver(win).frame();
  CHECK(a->bounds() == (Rect{20, 20, 160, 30}));
  // 300 - 40 padding - 90 heights - 20 spacing = 150 left over, all to b.
  CHECK(b->bounds() == (Rect{20, 60, 160, 190}));
  CHECK(c->bounds() == (Rect{20, 260, 160, 20}));
}

TEST(layout_row_alignment_and_justify) {
  App app(headless());
  auto win = app.open_window({"t", {300, 100}});
  auto row = win->add<Row>(StackOptions{0, {}, Align::center, Align::end});
  auto a = row->add<Box>(50, 20);
  auto b = row->add<Box>(50, 40);
  b->set_align(Align::start);
  testing::Driver(win).frame();
  CHECK(a->bounds() == (Rect{200, 40, 50, 20}));  // justified to the end, centred across
  CHECK(b->bounds() == (Rect{250, 0, 50, 40}));   // its own alignment wins
}

TEST(layout_spacer_pushes_apart_and_hidden_children_take_no_space) {
  App app(headless());
  auto win = app.open_window({"t", {300, 50}});
  auto row = win->add<Row>(StackOptions{0, {}, Align::start, Align::start});
  auto a = row->add<Box>(50, 20);
  row->add<Spacer>();
  auto hidden = row->add<Box>(70, 20);
  hidden->set_visible(false);
  auto b = row->add<Box>(50, 20);
  testing::Driver(win).frame();
  CHECK_EQ(a->bounds().x, 0.0f);
  CHECK_EQ(b->bounds().x, 250.0f);
}

TEST(layout_grid_fit_and_stretch_columns_with_span) {
  App app(headless());
  auto win = app.open_window({"t", {400, 200}});
  GridOptions o;
  o.columns = {GridTrack::fit(), GridTrack::stretch()};
  o.column_spacing = 10;
  o.row_spacing = 5;
  auto grid = win->add<Grid>(o);
  auto label1 = grid->add<Box>(60, 20);
  auto field1 = grid->add<Box>(30, 30);
  auto label2 = grid->add<Box>(90, 20);
  auto field2 = grid->add<Box>(30, 30);
  auto wide = grid->add<Box>(10, 10);
  wide->set_column_span(2);
  wide->set_align(Align::stretch);
  testing::Driver(win).frame();
  CHECK_EQ(label1->bounds().width, 90.0f);  // the fit column is as wide as its widest cell
  CHECK_EQ(field1->bounds().x, 100.0f);     // 90 + 10 spacing
  CHECK_EQ(field1->bounds().width, 300.0f); // the rest of the 400
  CHECK_EQ(field2->bounds().y, 35.0f);      // row 1 starts after row 0 (30 tall) and spacing
  CHECK_EQ(label2->bounds().y, 40.0f);      // and a 20-pixel label is centred in it
  CHECK(wide->bounds() == (Rect{0, 70, 400, 10}));
  CHECK_EQ(label1->bounds().y, 5.0f);       // centred in its 30-pixel row
}

TEST(layout_card_pads_its_children) {
  App app(headless());
  auto win = app.open_window({"t", {300, 300}});
  auto col = win->add<Column>(StackOptions{0, Insets::all(10), Align::start, Align::start});
  auto card = col->add<Card>(CardOptions{Insets::all(20), 8});
  auto a = card->add<Box>(100, 30);
  auto b = card->add<Box>(60, 30);
  testing::Driver(win).frame();
  CHECK(card->bounds() == (Rect{10, 10, 140, 108}));
  CHECK(a->bounds() == (Rect{30, 30, 100, 30}));
  CHECK(b->bounds() == (Rect{30, 68, 100, 30}));  // cards stretch children across
}
