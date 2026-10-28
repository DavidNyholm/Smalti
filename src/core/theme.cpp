// The three built-in themes. Dark is very dark grey (kind to LCDs, where pure black glows grey
// or smears); Black is true black for OLED, with depth from borders instead of shadows.
#include "smalti/theme.hpp"

namespace smalti {

namespace {
constexpr Color white(float a) { return {1, 1, 1, a}; }
constexpr Color black(float a) { return {0, 0, 0, a}; }
}  // namespace

const Theme& theme_dark() {
  static const Theme t = [] {
    Theme th;
    th.name = "Dark";
    th.titlebar = Color::hex(0x121316);
    th.sidebar = Color::hex(0x16171A);
    th.background = Color::hex(0x1B1C20);
    th.raised = Color::hex(0x222328);
    th.border = white(0.07f);
    th.divider = white(0.06f);
    th.hover = white(0.04f);
    th.selected = white(0.08f);
    th.text = Color::hex(0xE7E7EA);
    th.text_secondary = Color::hex(0xA3A4AD);
    th.text_muted = Color::hex(0x6E7079);
    th.accent = Color::hex(0x5B8CF0);
    th.accent_hover = Color::hex(0x6E9AF2);
    th.on_accent = Color::hex(0xFFFFFF);
    th.accent_soft = Color::hex(0x5B8CF0, 0.18f);
    th.success = Color::hex(0x3DBB7E);
    th.warning = Color::hex(0xE0A23B);
    th.danger = Color::hex(0xE5544B);
    th.shadows = true;
    th.shadow = black(0.35f);
    th.top_highlight = white(0.0f);
    th.dark = true;
    return th;
  }();
  return t;
}

const Theme& theme_black() {
  static const Theme t = [] {
    Theme th;
    th.name = "Black";
    th.titlebar = Color::hex(0x000000);
    th.sidebar = Color::hex(0x000000);
    th.background = Color::hex(0x000000);
    th.raised = Color::hex(0x0D0E10);
    th.border = white(0.09f);
    th.divider = white(0.08f);
    th.hover = white(0.05f);
    th.selected = white(0.09f);
    th.text = Color::hex(0xE3E3E6);  // off-white: easier on the eyes at OLED contrast
    th.text_secondary = Color::hex(0x9C9DA6);
    th.text_muted = Color::hex(0x63656E);
    th.accent = Color::hex(0x5585E8);
    th.accent_hover = Color::hex(0x6892EC);
    th.on_accent = Color::hex(0xFFFFFF);
    th.accent_soft = Color::hex(0x5585E8, 0.18f);
    th.success = Color::hex(0x35A86F);
    th.warning = Color::hex(0xD49A35);
    th.danger = Color::hex(0xDB4F46);
    th.shadows = false;  // a shadow on pure black cannot be seen
    th.shadow = black(0.0f);
    th.top_highlight = white(0.05f);
    th.dark = true;
    return th;
  }();
  return t;
}

const Theme& theme_light() {
  static const Theme t = [] {
    Theme th;
    th.name = "Light";
    th.titlebar = Color::hex(0xEEEFF2);
    th.sidebar = Color::hex(0xF3F4F6);
    th.background = Color::hex(0xFAFAFB);
    th.raised = Color::hex(0xFFFFFF);
    th.border = black(0.09f);
    th.divider = black(0.07f);
    th.hover = black(0.04f);
    th.selected = black(0.07f);
    th.text = Color::hex(0x1A1B1F);
    th.text_secondary = Color::hex(0x5B5D66);
    th.text_muted = Color::hex(0x8C8E97);
    th.accent = Color::hex(0x2F6DEB);
    th.accent_hover = Color::hex(0x255FD6);
    th.on_accent = Color::hex(0xFFFFFF);
    th.accent_soft = Color::hex(0x2F6DEB, 0.12f);
    th.success = Color::hex(0x1F9D62);
    th.warning = Color::hex(0xC98A1B);
    th.danger = Color::hex(0xD93A31);
    th.shadows = true;
    th.shadow = black(0.09f);
    th.top_highlight = white(0.0f);
    th.dark = false;
    return th;
  }();
  return t;
}

}  // namespace smalti
