// Mouse and keyboard events as widgets receive them, in logical pixels.
#pragma once

#include <cstdint>

#include "smalti/geometry.hpp"

namespace smalti {

enum class MouseButton : uint8_t { none, left, right, middle };

struct Modifiers {
  bool shift = false, ctrl = false, alt = false, super = false;
  friend bool operator==(Modifiers, Modifiers) = default;
};

struct MouseEvent {
  enum class Kind : uint8_t { move, press, release, wheel, enter, leave };
  Kind kind = Kind::move;
  Point position;  // window coordinates
  MouseButton button = MouseButton::none;
  Modifiers modifiers;
  Point wheel;     // scroll distance in logical pixels, positive = down and right
  int clicks = 1;  // 2 for the second press of a double-click
};

// Keys that have a meaning in widgets. Letters and digits are reported for shortcuts; typed text
// arrives separately (from v0.2), already composed by the platform's input method.
enum class Key : uint16_t {
  unknown,
  a,
  b,
  c,
  d,
  e,
  f,
  g,
  h,
  i,
  j,
  k,
  l,
  m,
  n,
  o,
  p,
  q,
  r,
  s,
  t,
  u,
  v,
  w,
  x,
  y,
  z,
  digit0,
  digit1,
  digit2,
  digit3,
  digit4,
  digit5,
  digit6,
  digit7,
  digit8,
  digit9,
  space,
  enter,
  escape,
  tab,
  backspace,
  delete_,
  left,
  right,
  up,
  down,
  home,
  end,
  page_up,
  page_down,
  f1,
  f2,
  f3,
  f4,
  f5,
  f6,
  f7,
  f8,
  f9,
  f10,
  f11,
  f12,
};

struct KeyEvent {
  Key key = Key::unknown;
  bool down = true;
  bool repeat = false;
  Modifiers modifiers;
};

enum class Cursor : uint8_t { arrow, hand, text, resize_horizontal, resize_vertical };

}  // namespace smalti
