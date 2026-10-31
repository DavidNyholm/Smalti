# Widgets

Every built-in control, what it does, and how it works from the keyboard. All of them follow the theme, animate their state changes, and draw at 45% opacity when disabled. A theme file can restyle each of them; [Themes](Themes.md#widget-styles) lists every widget's colours and states.

| Widget | Created with | Events | Keyboard |
|---|---|---|---|
| `Label` | `add<Label>("text", LabelOptions{.role, .wrap})` | none | not focusable |
| `Button` | `add<Button>("text", ButtonVariant::primary)` | `on_click()` | Space or Enter |
| `CheckBox` | `add<CheckBox>("text", checked)` | `on_change(bool)` | Space |
| `Switch` | `add<Switch>("text", on)` | `on_change(bool)` | Space or Enter |
| `RadioButton` | `add<RadioButton>("text", selected)` | `on_select()` | Space; arrows move within the group |
| `Slider` | `add<Slider>(SliderOptions{.min, .max, .value, .step})` | `on_change(float)` | arrows, Page Up and Down, Home, End |
| `TextBox` (in `beta`) | `add<TextBox>(TextBoxOptions{.text, .placeholder, .password, .max_length})`, or `add<TextBox>("placeholder")` | `on_change(const std::string&)`, `on_submit(const std::string&)` | typing, and the keys below |
| `ProgressBar` | `add<ProgressBar>(0.4f)` | none | not focusable |
| `Separator` | `add<Separator>()` | none | not focusable |
| `Card` | `add<Card>()` | none | a container |

**Labels** have five roles: `body`, `secondary` (supporting text in the secondary colour), `caption` (small and muted), `title` and `heading`. With `.wrap = true` a label breaks into lines to fit the width its parent gives it.

**Buttons** come in four variants: `primary` (the accent colour, for the one main action of a view), `secondary` (the default, a raised surface with a border), `ghost` (text only), and `danger`. A button clicks when the pointer is released over it; dragging off before releasing cancels the click.

**Radio buttons** that share a parent form one group: selecting one clears the others. Tab stops only at the selected button of a group, and the arrow keys move the selection, as in native toolkits.

**Sliders** snap to `step` when it is non-zero. The arrow keys move one step, or 1% of the range without a step; Page Up and Page Down move 10%.

**Progress bars** animate to a new value. `set_indeterminate(true)` shows a moving segment for work of unknown length; it draws only while visible.

**Text boxes** (in `beta`, for v0.2) hold one line of text. Typing goes through the system's input method, so dead keys, compose sequences and IMEs work, with the IME's windows at the caret ([Events and threading](Events-and-threading.md#typed-text-and-the-input-method)).

- **Keys:** the arrows move by character, and with Ctrl by word; Home and End go to either end; Shift with any of them selects. Backspace and Delete erase a character, or with Ctrl a word. Ctrl+A selects everything. Ctrl+C, Ctrl+X and Ctrl+V copy, cut and paste, as do Ctrl+Insert, Shift+Delete and Shift+Insert. Ctrl+Z undoes; Ctrl+Y and Ctrl+Shift+Z redo. Enter calls `on_submit` when one is set, and otherwise goes on to the window, as Escape and Tab always do.
- **Mouse:** a click places the caret, a drag selects (and scrolls when it passes either end), a double click selects a word, a triple click everything, and Shift+click extends the selection.
- **Words** are runs of letters, digits and underscores (any character beyond ASCII counts as a letter), of punctuation, or of spaces. Ctrl+Right goes past a word and the spaces after it, as in Windows editors.
- **Characters are code points:** an emoji with a skin tone, or a letter followed by a separate combining accent, takes more than one step of the caret. Precomposed letters, which keyboards type, are one. Grapheme clusters come with complex scripts, after v0.5.
- **Undo** keeps typing together a word at a time; a pause of a second or a caret move starts a new step, and a paste is a step of its own.
- **Pasted text** is made one line: line breaks inside become spaces, those at either end are dropped, and tabs become spaces.
- **`password = true`** shows a dot for each character, and refuses copy and cut. **`max_length`** counts characters; typing and pasting stop at it.
- **The caret** blinks at the system's rate (Windows' setting; 530 ms elsewhere) on timed redraws, so a focused text box costs about two frames a second, and none once the caret stops blinking, 10 seconds after the last input. It is one device pixel wide at any scale.
- `on_change` follows every change the user makes; `set_text()` does not call it.

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
