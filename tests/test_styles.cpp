// Widget styles: the built-in look against the 0.1 formulas it replaces, the "widgets" section
// of a theme file (every form, precedence, every error), and widgets drawing with a style.
#include <algorithm>
#include <cmath>
#include <string>

#include "check.hpp"
#include "support.hpp"
#include "theme/widget_styles.hpp"

using namespace smalti;
using detail::StyledWidget;
using detail::StyleMix;

namespace {

bool contains(std::string_view s, std::string_view part) {
  return s.find(part) != std::string_view::npos;
}

bool near(Color a, Color b) {
  constexpr float kTolerance = 1e-5f;
  return std::fabs(a.r - b.r) <= kTolerance && std::fabs(a.g - b.g) <= kTolerance &&
         std::fabs(a.b - b.b) <= kTolerance && std::fabs(a.a - b.a) <= kTolerance;
}

ThemeError first_error(std::string_view text) {
  const ThemeLoad r = parse_theme(text);
  return r.errors.empty() ? ThemeError{-1, -1, "no error"} : r.errors.front();
}

constexpr float kDim = 0.45f;  // 0.1's disabled opacity

}  // namespace

// The defaults are the 0.1 drawing code's colours, written as styles. At rest and at the ends
// of each animation they must be exactly what 0.1 computed; in between, within float rounding.
TEST(styles_default_to_the_0_1_look) {
  using detail::ButtonSlot;
  for (const Theme* theme : {&theme_dark(), &theme_black(), &theme_light()}) {
    const Theme& th = *theme;
    const detail::WidgetStyles styles = detail::compute_widget_styles(th);
    const auto button = [&](ButtonVariant v) -> const detail::WidgetStyle& {
      return styles.get(StyledWidget::button, static_cast<size_t>(v));
    };
    const detail::WidgetStyle& primary = button(ButtonVariant::primary);
    for (const float h : {0.0f, 0.5f, 1.0f}) {
      CHECK(primary.color(ButtonSlot::fill, {.hover = h}) ==
            Color::mix(th.accent, th.accent_hover, h));
    }
    CHECK(primary.color(ButtonSlot::text, {}) == th.on_accent);
    CHECK(primary.color(ButtonSlot::fill, {.disabled = true}) ==
          th.accent.with_alpha(th.accent.a * kDim));
    CHECK_EQ(primary.radius, th.radius_control);
    CHECK(button(ButtonVariant::secondary).color(ButtonSlot::fill, {}) == th.raised);
    CHECK(button(ButtonVariant::secondary).color(ButtonSlot::border, {}) == th.border);
    CHECK(button(ButtonVariant::ghost).color(ButtonSlot::fill, {}).a == 0);
    CHECK(button(ButtonVariant::ghost).color(ButtonSlot::text, {}) == th.accent);
    // 0.1 drew the ghost's hover tint and the press shade as layers over the fill.
    const Color tint = th.hover.with_alpha(th.hover.a * 1.5f);
    CHECK(near(button(ButtonVariant::ghost).color(ButtonSlot::fill, {.hover = 1}), tint));
    CHECK(near(button(ButtonVariant::secondary).color(ButtonSlot::fill, {.hover = 1}),
               detail::over(th.raised, tint)));
    CHECK(near(primary.color(ButtonSlot::fill, {.hover = 1, .pressed = 1}),
               detail::over(th.accent_hover, {0, 0, 0, 0.12f})));
    const detail::WidgetStyle& danger = button(ButtonVariant::danger);
    CHECK(danger.color(ButtonSlot::fill, {}) == th.danger.with_alpha(0.14f));
    CHECK(danger.color(ButtonSlot::fill, {.hover = 1}) ==
          th.danger.with_alpha(0.14f + 0.06f * 1.0f));
    CHECK(danger.color(ButtonSlot::text, {}) == th.danger);

    using detail::CheckBoxSlot;
    const detail::WidgetStyle& box = styles.get(StyledWidget::check_box);
    for (const float t : {0.0f, 0.5f, 1.0f}) {
      CHECK(box.color(CheckBoxSlot::fill, {.checked = t}) == Color::mix(th.raised, th.accent, t));
      const Color edge = Color::mix(th.text_muted, th.text_secondary, t);
      CHECK(box.color(CheckBoxSlot::border, {.hover = t}) == edge);
    }
    // 0.1 faded the outline out as the box filled, and drew none once it was full.
    CHECK(near(box.color(CheckBoxSlot::border, {.hover = 1, .checked = 0.5f}),
               th.text_secondary.with_alpha(th.text_secondary.a * 0.5f)));
    CHECK(box.color(CheckBoxSlot::border, {.hover = 1, .checked = 1}).a == 0);
    CHECK(box.color(CheckBoxSlot::mark, {}) == th.on_accent);
    CHECK_EQ(box.radius, 5.0f);

    using detail::SwitchSlot;
    const detail::WidgetStyle& sw = styles.get(StyledWidget::switch_toggle);
    for (const float h : {0.0f, 1.0f}) {
      for (const float t : {0.0f, 1.0f}) {
        const Color knob =
            Color::mix(Color::mix(th.text_secondary, th.text, h * 0.5f), th.on_accent, t);
        CHECK(near(sw.color(SwitchSlot::knob, {.hover = h, .checked = t}), knob));
        CHECK(near(sw.color(SwitchSlot::track, {.checked = t}),
                   Color::mix(th.selected, th.accent, t)));
      }
    }
    CHECK(sw.color(SwitchSlot::border, {}) == th.border);
    CHECK(sw.color(SwitchSlot::border, {.checked = 1}).a == 0);

    using detail::RadioSlot;
    const detail::WidgetStyle& radio = styles.get(StyledWidget::radio_button);
    for (const float h : {0.0f, 1.0f}) {
      for (const float t : {0.0f, 1.0f}) {
        CHECK(radio.color(RadioSlot::border, {.hover = h, .checked = t}) ==
              Color::mix(Color::mix(th.text_muted, th.text_secondary, h), th.accent, t));
      }
    }
    CHECK(radio.color(RadioSlot::fill, {}) == th.raised);
    CHECK(radio.color(RadioSlot::mark, {}) == th.accent);

    const detail::WidgetStyle& slider = styles.get(StyledWidget::slider);
    CHECK(slider.color(detail::SliderSlot::track, {}) == th.selected);
    CHECK(slider.color(detail::SliderSlot::fill, {}) == th.accent);
    CHECK(slider.color(detail::SliderSlot::knob, {}) == Color::hex(0xFFFFFF));
    CHECK(slider.color(detail::SliderSlot::border, {}) == th.border);
    const detail::WidgetStyle& progress = styles.get(StyledWidget::progress_bar);
    CHECK(progress.color(detail::ProgressSlot::track, {}) == th.selected);
    CHECK(progress.color(detail::ProgressSlot::fill, {}) == th.accent);
    CHECK(styles.get(StyledWidget::separator).color(detail::SeparatorSlot::fill, {}) == th.divider);
    const Color label[] = {th.text, th.text_secondary, th.text_muted, th.text, th.text};
    for (size_t role = 0; role < std::size(label); ++role) {
      CHECK(styles.get(StyledWidget::label, role).color(detail::LabelSlot::text, {}) ==
            label[role]);
    }
    const detail::WidgetStyle& card = styles.get(StyledWidget::card);
    CHECK(card.color(detail::CardSlot::fill, {}) == th.raised);
    CHECK(card.color(detail::CardSlot::border, {}) == th.border);
    CHECK_EQ(card.radius, th.radius_card);
  }
}

TEST(styles_read_every_value_form_from_a_theme_file) {
  using detail::ButtonSlot;
  const ThemeLoad r = parse_theme(R"({
    "tokens": {"accent": "#ff0000"},
    "widgets": {
      "Button": {"radius": 4, "focus": "#00ff00", "focused": {"border": "#ff00ff"}},
      "Button.primary": {
        "fill": "#123456",
        "hover": {"fill": "accent 50%"},
        "disabled": {"opacity": "20%"},
      },
      "CheckBox": {"checked": {"fill": "status.success"}, "radius": "radius.card"},
      "Label.caption": {"text": "#abcdef"},
      "Switch": {"disabled": {"track": "#000000"}},
    },
  })");
  CHECK(r.ok());
  CHECK_EQ(r.theme.widget_styles.size(), size_t{10});
  const detail::WidgetStyles styles = detail::compute_widget_styles(r.theme);
  const detail::WidgetStyle& primary = styles.get(StyledWidget::button, 0);
  CHECK(primary.color(ButtonSlot::fill, {}) == Color::hex(0x123456));
  CHECK(primary.color(ButtonSlot::fill, {.hover = 1}) == Color::hex(0xFF0000, 0.5f));
  // The default pressed look darkens the hover fill the file set.
  CHECK(near(primary.color(ButtonSlot::fill, {.hover = 1, .pressed = 1}),
             detail::over(Color::hex(0xFF0000, 0.5f), {0, 0, 0, 0.12f})));
  CHECK(near(primary.color(ButtonSlot::fill, {.disabled = true}), Color::hex(0x123456, 0.2f)));
  for (size_t v = 0; v < 4; ++v) {
    CHECK_EQ(styles.get(StyledWidget::button, v).radius, 4.0f);
    CHECK(styles.get(StyledWidget::button, v).color(ButtonSlot::focus, {}) == Color::hex(0x00FF00));
  }
  CHECK(styles.get(StyledWidget::button, 1).color(ButtonSlot::fill, {}) == theme_dark().raised);
  // "focused" applies at once, while the control has keyboard focus.
  CHECK(primary.color(ButtonSlot::border, {.focused = true}) == Color::hex(0xFF00FF));
  CHECK(primary.color(ButtonSlot::border, {}).a == 0);
  const detail::WidgetStyle& box = styles.get(StyledWidget::check_box);
  CHECK(near(box.color(detail::CheckBoxSlot::fill, {.checked = 1}), theme_dark().success));
  CHECK_EQ(box.radius, theme_dark().radius_card);
  CHECK(styles.get(StyledWidget::label, static_cast<size_t>(TextRole::caption))
            .color(detail::LabelSlot::text, {}) == Color::hex(0xABCDEF));
  CHECK(styles.get(StyledWidget::label, 0).color(detail::LabelSlot::text, {}) == theme_dark().text);
  // A colour set for "disabled" replaces the colour; the others still dim.
  const detail::WidgetStyle& sw = styles.get(StyledWidget::switch_toggle);
  CHECK(sw.color(detail::SwitchSlot::track, {.checked = 1, .disabled = true}) ==
        Color::hex(0x000000));
  CHECK(sw.color(detail::SwitchSlot::text, {.disabled = true}).a == theme_dark().text.a * kDim);
}

TEST(styles_one_variant_wins_over_every_variant_whatever_the_order) {
  using detail::ButtonSlot;
  const ThemeLoad r = parse_theme(R"({"widgets": {
    "Button.primary": {"fill": "#111111"},
    "Button": {"fill": "#222222"},
  }})");
  CHECK(r.ok());
  const detail::WidgetStyles styles = detail::compute_widget_styles(r.theme);
  CHECK(styles.get(StyledWidget::button, 0).color(ButtonSlot::fill, {}) == Color::hex(0x111111));
  CHECK(styles.get(StyledWidget::button, 1).color(ButtonSlot::fill, {}) == Color::hex(0x222222));
  // A secondary button's hover still lightens the fill it now has.
  const Color tint = theme_dark().hover.with_alpha(theme_dark().hover.a * 1.5f);
  CHECK(near(styles.get(StyledWidget::button, 1).color(ButtonSlot::fill, {.hover = 1}),
             detail::over(Color::hex(0x222222), tint)));
}

TEST(styles_report_each_error_where_it_is) {
  struct Case {
    const char* text;
    int line, column;
    const char* says;
  };
  const Case cases[] = {
      {R"({"widgets": []})", 1, 13, "\"widgets\" is an object"},
      {R"({"widgets": {"Buton": {}}})", 1, 14,
       "unknown widget \"Buton\"; did you mean \"Button\"?"},
      {R"({"widgets": {"CheckBox.big": {}}})", 1, 14, "\"CheckBox\" has no variants"},
      {R"({"widgets": {"Button.primry": {}}})", 1, 14, "did you mean \"primary\"?"},
      {R"({"widgets": {"Button": 5}})", 1, 24, "\"Button\" is an object of properties and states"},
      {R"({"widgets": {"Button": {"checked": {}}}})", 1, 25,
       "\"Button\" has no \"checked\" state; its states are hover, pressed, focused and disabled"},
      {R"({"widgets": {"Separator": {"hover": {}}}})", 1, 28, "it has no states"},
      {R"({"widgets": {"Button": {"hover": {"pressed": {}}}}})", 1, 35,
       "\"pressed\" cannot go inside \"hover\""},
      {R"({"widgets": {"Button": {"hover": {"radius": 4}}}})", 1, 35,
       "\"radius\" is set at the top"},
      {R"({"widgets": {"Button": {"opacity": "50%"}}})", 1, 25,
       "\"opacity\" is set under \"disabled\""},
      {R"({"widgets": {"Button": {"fil": "#ffffff"}}})", 1, 25,
       "unknown property \"fil\" of \"Button\"; did you mean \"fill\"?"},
      {R"({"widgets": {"Button": {"disabled": {"opacity": 0.5}}}})", 1, 49,
       "\"opacity\" takes a percentage"},
      {R"({"widgets": {"Button": {"fill": 5}}})", 1, 33, "\"fill\" takes a colour"},
      {R"({"widgets": {"Button": {"radius": "accent"}}})", 1, 35, "\"accent\" is a colour"},
      {R"({"widgets": {"Button": {}, "Button": {}}})", 1, 28, "set twice"},
      {R"({"widgets": {"Button": {"fill": "#ffffff", "fill": "#000000"}}})", 1, 44, "set twice"},
  };
  for (const Case& c : cases) {
    const ThemeError e = first_error(c.text);
    if (e.line != c.line || e.column != c.column || !contains(e.message, c.says)) {
      check::fail(std::string(c.text) + "  ->  " + std::to_string(e.line) + ":" +
                      std::to_string(e.column) + " " + e.message,
                  __FILE__, __LINE__);
    }
  }
}

TEST(styles_change_what_widgets_draw) {
  const ThemeLoad r = parse_theme(R"({"widgets": {
    "Button.primary": {"fill": "#ff0000", "disabled": {"opacity": "100%"}},
    "Label.caption": {"text": "#ff0000"},
  }})");
  CHECK(r.ok());
  App app(headless(&r.theme));
  auto win = app.open_window({"t", {300, 100}});
  auto row = win->add<Row>(StackOptions{8, Insets::all(20), Align::start, Align::start});
  auto red = row->add<Button>("Red", ButtonVariant::primary);
  auto off = row->add<Button>("Off", ButtonVariant::primary);
  off->set_enabled(false);
  auto caption = row->add<Label>("Caption", LabelOptions{.role = TextRole::caption});
  auto body = row->add<Label>("Body");
  testing::Driver d(win);
  // Text is anti-aliased, so look for its reddest pixel: the caption's style is red, the
  // body text's is not.
  const auto reddest = [&](const Rect& b) {
    int most = -255;
    for (int y = static_cast<int>(b.y); y < static_cast<int>(b.bottom()); ++y) {
      for (int x = static_cast<int>(b.x); x < static_cast<int>(b.right()); ++x) {
        const uint32_t px = d.pixel(x, y);
        most = std::max(most, static_cast<int>(px >> 16 & 0xFF) - static_cast<int>(px >> 8 & 0xFF));
      }
    }
    return most;
  };
  constexpr int kClearlyRed = 128;
  CHECK(reddest(caption->bounds()) > kClearlyRed);
  CHECK(reddest(body->bounds()) < kClearlyRed);
  const auto left_inside = [&](const Rect& b) {
    return d.pixel(static_cast<int>(b.x + 3), static_cast<int>(b.center().y)) & 0xFFFFFF;
  };
  CHECK_EQ(left_inside(red->bounds()), 0xFF0000u);
  // With the disabled opacity at 100%, a disabled button draws at full strength.
  CHECK_EQ(left_inside(off->bounds()), 0xFF0000u);
  // A theme set later restyles the window: back to Dark's accent.
  app.set_theme(theme_dark());
  d.frame();
  CHECK_EQ(left_inside(red->bounds()), 0x5B8CF0u);
}

TEST(styles_a_rule_naming_nothing_is_refused_and_changes_nothing) {
  App app(headless());
  ExpectContracts expect;
  Theme t = theme_dark();
  t.widget_styles.push_back({"Buton", "", "", "fill", Color::hex(0xFF0000)});
  bool refused = false;
  try {
    app.set_theme(t);
  } catch (const ContractError& e) {
    refused = contains(e.what(), "names no widget");
  }
  CHECK(refused);
  CHECK(app.theme() == theme_dark());
  // The same mistakes as the loader catches, for rules built in code.
  t.widget_styles = {{"Button", "", "checked", "fill", {}}};
  refused = false;
  try {
    app.set_theme(t);
  } catch (const ContractError&) {
    refused = true;
  }
  CHECK(refused);
}

TEST(styles_blend_through_premultiplied_colour) {
  const Color red{1, 0, 0, 1}, clear_blue{0, 0, 1, 0};
  CHECK(detail::blend_colors(red, clear_blue, 0) == red);
  CHECK(detail::blend_colors(red, clear_blue, 1) == clear_blue);
  // Fading to transparent keeps the hue: no blue appears on the way.
  const Color half = detail::blend_colors(red, clear_blue, 0.5f);
  CHECK(near(half, {1, 0, 0, 0.5f}));
  // Colours of the same opacity mix as Color::mix does.
  const Color a{0.2f, 0.4f, 0.6f, 0.5f}, b{0.6f, 0.2f, 0.0f, 0.5f};
  CHECK(detail::blend_colors(a, b, 0.25f) == Color::mix(a, b, 0.25f));
  // Black at half opacity over white is mid grey, opaque.
  CHECK(near(detail::over({1, 1, 1, 1}, {0, 0, 0, 0.5f}), {0.5f, 0.5f, 0.5f, 1}));
  CHECK(detail::over({0, 0, 0, 0}, {0, 0, 0, 0}).a == 0);
}
