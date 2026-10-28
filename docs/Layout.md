# Layout

Containers measure their children, then share out the space. Sizes are logical pixels; edges are snapped to device pixels when drawn, so borders stay crisp at 125% and 150%.

## Column and Row

`Column` stacks its children top to bottom, `Row` left to right. Both take `StackOptions`:

- `spacing`: the gap between children.
- `padding`: space inside the edges (`Insets::all(16)`, `Insets::symmetric(24, 12)`).
- `align`: where children sit across the stack: `start`, `center`, `end`, or `stretch` (the default, which fills the width of a column or the height of a row). A child's own `set_align()` overrides it.
- `justify`: where the children sit along the stack when none of them stretches: `start`, `center` or `end`.

A child with `set_stretch(1)` takes a share of the leftover space; two children with stretch 1 and 2 split it one third to two thirds. A hidden child (`set_visible(false)`) takes no space.

## Spacer

Empty space with stretch 1 by default: `row->add<Spacer>()` between two buttons pushes them to opposite ends. `Spacer(0, 16)` is a fixed 16-pixel gap.

## Grid

`GridOptions{.columns = {...}}` defines the columns; children fill the cells left to right, then down.

- `GridTrack::fixed(120)`: exactly 120 pixels.
- `GridTrack::fit()`: as wide as its widest cell.
- `GridTrack::stretch(weight)`: a share of what is left.

`set_column_span(2)` makes a child cover two columns. Within a row, cells are centred vertically unless their alignment says otherwise.

## Card

A `Card` is a `Column` drawn as a raised surface (a soft shadow, or a top highlight on the Black theme) with 20 pixels of padding and 12 of spacing by default.

## The window

A window's children fill it. Most programs add one `Column` or `Row` and build from there.
