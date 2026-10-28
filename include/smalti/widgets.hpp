// The built-in controls of v0.1. Each one follows the theme, animates its state changes, and
// works by keyboard as well as by mouse.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "smalti/callback.hpp"
#include "smalti/widget.hpp"

namespace smalti {

enum class TextRole : uint8_t {
  body,       // regular text
  secondary,  // supporting text, in the secondary colour
  caption,    // small and muted
  title,      // a section title
  heading,    // the page heading
};

struct LabelOptions {
  TextRole role = TextRole::body;
  bool wrap = false;  // break into lines to fit the width it is given
};

class Label : public Widget {
 public:
  explicit Label(std::string text = {}, LabelOptions options = {});
  void set_text(std::string text);
  const std::string& text() const { return text_; }

  Size measure(Size available) override;
  void arrange(Rect bounds) override;
  void paint(Painter& p) override;

 private:
  TextStyle style() const;
  Color color() const;
  std::string text_;
  LabelOptions options_;
  std::vector<std::string_view> lines_;  // wrapped lines, recomputed when the width changes
};

enum class ButtonVariant : uint8_t { primary, secondary, ghost, danger };

struct ButtonOptions {
  std::string text;
  ButtonVariant variant = ButtonVariant::secondary;
};

class Button : public Widget {
 public:
  explicit Button(ButtonOptions options);
  explicit Button(std::string text, ButtonVariant variant = ButtonVariant::secondary)
      : Button(ButtonOptions{std::move(text), variant}) {}

  Button& on_click(Callback<void()> fn);
  void set_text(std::string text);
  const std::string& text() const { return options_.text; }

  Size measure(Size available) override;
  void paint(Painter& p) override;
  bool on_mouse(const MouseEvent& e) override;
  bool on_key(const KeyEvent& e) override;
  void on_hover_changed(bool hovered) override;
  void on_focus_changed(bool gained) override;
  bool focusable() const override { return enabled(); }
  Cursor cursor() const override { return Cursor::hand; }

 private:
  void press_visual(bool down);
  void click();
  ButtonOptions options_;
  Callback<void()> on_click_;
  Motion hover_, press_;
};

struct CheckBoxOptions {
  std::string text;
  bool checked = false;
};

class CheckBox : public Widget {
 public:
  explicit CheckBox(CheckBoxOptions options);
  explicit CheckBox(std::string text, bool checked = false)
      : CheckBox(CheckBoxOptions{std::move(text), checked}) {}

  CheckBox& on_change(Callback<void(bool)> fn);
  void set_checked(bool checked);
  bool checked() const { return checked_; }

  Size measure(Size available) override;
  void paint(Painter& p) override;
  bool on_mouse(const MouseEvent& e) override;
  bool on_key(const KeyEvent& e) override;
  void on_hover_changed(bool hovered) override;
  bool focusable() const override { return enabled(); }
  Cursor cursor() const override { return Cursor::hand; }

 private:
  void toggle();
  std::string text_;
  bool checked_;
  Callback<void(bool)> on_change_;
  Motion check_, hover_;
};

struct SwitchOptions {
  std::string text;
  bool on = false;
};

class Switch : public Widget {
 public:
  explicit Switch(SwitchOptions options);
  explicit Switch(std::string text, bool on = false) : Switch(SwitchOptions{std::move(text), on}) {}

  Switch& on_change(Callback<void(bool)> fn);
  void set_on(bool on);
  bool is_on() const { return on_; }

  Size measure(Size available) override;
  void paint(Painter& p) override;
  bool on_mouse(const MouseEvent& e) override;
  bool on_key(const KeyEvent& e) override;
  void on_hover_changed(bool hovered) override;
  bool focusable() const override { return enabled(); }
  Cursor cursor() const override { return Cursor::hand; }

 private:
  void toggle();
  std::string text_;
  bool on_;
  Callback<void(bool)> on_change_;
  Motion knob_, hover_;
};

// Radio buttons that share a parent form one group: selecting one clears its siblings, and the
// arrow keys move the selection within the group.
class RadioButton : public Widget {
 public:
  explicit RadioButton(std::string text, bool selected = false);

  RadioButton& on_select(Callback<void()> fn);
  void select();
  bool selected() const { return selected_; }

  Size measure(Size available) override;
  void paint(Painter& p) override;
  bool on_mouse(const MouseEvent& e) override;
  bool on_key(const KeyEvent& e) override;
  void on_hover_changed(bool hovered) override;
  bool focusable() const override { return enabled(); }
  bool tab_stop() const override { return enabled() && (selected_ || !group_has_selection()); }
  Cursor cursor() const override { return Cursor::hand; }

 private:
  void set_selected(bool selected, bool notify);
  bool group_has_selection() const;
  void move_selection(int step);
  std::string text_;
  bool selected_;
  Callback<void()> on_select_;
  Motion dot_, hover_;
};

struct SliderOptions {
  float min = 0, max = 1, value = 0;
  float step = 0;  // 0: continuous
};

class Slider : public Widget {
 public:
  explicit Slider(SliderOptions options = {});

  Slider& on_change(Callback<void(float)> fn);
  void set_value(float value);
  float value() const { return value_; }

  Size measure(Size available) override;
  void paint(Painter& p) override;
  bool on_mouse(const MouseEvent& e) override;
  bool on_key(const KeyEvent& e) override;
  void on_hover_changed(bool hovered) override;
  bool focusable() const override { return enabled(); }
  Cursor cursor() const override { return Cursor::hand; }
  float overflow() const override;  // the knob's shadow

 private:
  void set_from_pointer(float x);
  void change(float value);
  float fraction() const;
  SliderOptions options_;
  float value_;
  Callback<void(float)> on_change_;
  Motion hover_;
};

class ProgressBar : public Widget {
 public:
  explicit ProgressBar(float value = 0);
  void set_value(float value);  // 0..1
  float value() const { return value_; }
  void set_indeterminate(bool indeterminate);

  Size measure(Size available) override;
  void paint(Painter& p) override;

 private:
  float value_;
  bool indeterminate_ = false;
  Motion fill_, sweep_;
};

enum class Orientation : uint8_t { horizontal, vertical };

class Separator : public Widget {
 public:
  explicit Separator(Orientation orientation = Orientation::horizontal)
      : orientation_(orientation) {}
  Size measure(Size available) override;
  void paint(Painter& p) override;

 private:
  Orientation orientation_;
};

}  // namespace smalti
