# Widgets

Every control in v0.1, what it does, and how it works from the keyboard. All of them follow the theme, animate their state changes, and draw at 40% opacity when disabled.

| Widget | Created with | Events | Keyboard |
|---|---|---|---|
| `Label` | `add<Label>("text", LabelOptions{.role, .wrap})` | none | not focusable |
| `Button` | `add<Button>("text", ButtonVariant::primary)` | `on_click()` | Space or Enter |
| `CheckBox` | `add<CheckBox>("text", checked)` | `on_change(bool)` | Space |
| `Switch` | `add<Switch>("text", on)` | `on_change(bool)` | Space or Enter |
| `RadioButton` | `add<RadioButton>("text", selected)` | `on_select()` | Space; arrows move within the group |
| `Slider` | `add<Slider>(SliderOptions{.min, .max, .value, .step})` | `on_change(float)` | arrows, Page Up and Down, Home, End |
| `ProgressBar` | `add<ProgressBar>(0.4f)` | none | not focusable |
| `Separator` | `add<Separator>()` | none | not focusable |
| `Card` | `add<Card>()` | none | a container |

**Labels** have five roles: `body`, `secondary` (supporting text in the secondary colour), `caption` (small and muted), `title` and `heading`. With `.wrap = true` a label breaks into lines to fit the width its parent gives it.

**Buttons** come in four variants: `primary` (the accent colour, for the one main action of a view), `secondary` (the default, a raised surface with a border), `ghost` (text only), and `danger`. A button clicks when the pointer is released over it; dragging off before releasing cancels the click.

**Radio buttons** that share a parent form one group: selecting one clears the others. Tab stops only at the selected button of a group, and the arrow keys move the selection, as in native toolkits.

**Sliders** snap to `step` when it is non-zero. The arrow keys move one step, or 1% of the range without a step; Page Up and Page Down move 10%.

**Progress bars** animate to a new value. `set_indeterminate(true)` shows a moving segment for work of unknown length; it draws only while visible.

**Focus** moves with Tab and Shift+Tab in tree order. The focus ring shows only while the keyboard is being used, and hides again at the next mouse press.

## Writing a widget

Derive from `Widget` (or a container such as `Column`) and override what you need:

```cpp
class Swatch : public sm::Widget {
 public:
  explicit Swatch(sm::Color c) : color_(c) {}
  sm::Size measure(sm::Size) override { return {32, 32}; }
  void paint(sm::Painter& p) override { p.fill_rounded_rect(bounds(), 8, color_); }

 private:
  sm::Color color_;
};
```

`measure()` returns the size the widget wants, `paint()` draws it in logical pixels, and `on_mouse()`, `on_key()`, `focusable()` and `on_hover_changed()` add behaviour. `animate()` and `at()` run a value over time; `invalidate()` asks for a repaint. The gallery's sidebar is a complete example.
