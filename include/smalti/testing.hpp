// Driving a window without a screen: simulated input, a fake clock, and the drawn pixels.
// Use with AppOptions{.backend = Backend::headless}.
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "smalti/app.hpp"

namespace smalti::testing {

class Driver {
 public:
  explicit Driver(Ref<Window> window);

  void move(Point p);
  void press(Point p, MouseButton button = MouseButton::left);
  void release(Point p, MouseButton button = MouseButton::left);
  void click(Point p);
  void click(Ref<Widget> widget);  // at the centre of its bounds
  void wheel(Point p, Point delta);
  void key(Key key, Modifiers modifiers = {});
  // Text typed into the window, as a keyboard or an input method delivers it (after its keys).
  void type(std::string_view utf8);
  // Puts text on the headless clipboard as another program would: as it is, not cleaned, so
  // App::clipboard_text() shows what a paste from outside gets.
  void put_on_clipboard(std::string_view text);
  // What the headless clipboard holds, as is: what another program would paste.
  std::optional<std::string> clipboard_contents() const;
  void resize(Size logical, float scale = 1.0f);

  // Moves the fake clock forward, running every frame the window asks for on the way.
  void advance(std::chrono::milliseconds time);
  // Lays out and paints whatever is pending, now.
  void frame();
  // Processes pending removals and closes without drawing.
  void idle();

  int width() const;  // device pixels
  int height() const;
  uint32_t pixel(int x, int y) const;  // 0xAARRGGBB
  bool frame_requested() const;        // would a real window draw again without new input?
  int frames_drawn() const;
  Cursor cursor() const;  // the mouse cursor the window shows now
  // Whether the window's input method is switched on (a widget that takes text has focus), and
  // where its windows go: the caret, in logical pixels.
  bool text_input_active() const;
  Rect text_input_caret() const;

 private:
  Ref<Window> window_;
};

}  // namespace smalti::testing
