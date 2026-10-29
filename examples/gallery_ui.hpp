// The gallery's user interface, shared by the gallery program and the screenshot renderer:
// every v0.1 control, the three built-in themes, and a custom widget (the sidebar).
#pragma once

#include <cmath>
#include <filesystem>
#include <string>

#include <smalti/smalti.hpp>

namespace sm = smalti;

namespace gallery {

inline constexpr float kSidebarWidth = 220;

// A custom widget: a Column with the theme's sidebar colour and a divider on its right edge.
class Sidebar : public sm::Column {
 public:
  Sidebar() : sm::Column(sm::StackOptions{.spacing = 4, .padding = sm::Insets::all(16)}) {
    set_min_size({kSidebarWidth, 0});
  }
  void paint(sm::Painter& p) override {
    const sm::Rect b = bounds();
    p.fill_rect(b, theme().sidebar);
    p.fill_rect({b.right() - p.hairline(), b.y, p.hairline(), b.height}, theme().divider);
  }
};

inline sm::Ref<sm::Card> titled_card(sm::Ref<sm::Row> row, const char* title) {
  auto card = row->add<sm::Card>();
  card->set_stretch(1);
  card->add<sm::Label>(title, sm::LabelOptions{.role = sm::TextRole::title});
  return card;
}

// A theme file the gallery was started with. It is listed after the built-in themes, and
// watched while selected, so saving it restyles the gallery.
struct FileTheme {
  std::string name;
  std::filesystem::path path;
};

inline void build_sidebar(sm::App& app, sm::Ref<Sidebar> side, const FileTheme* file) {
  side->add<sm::Label>("smalti", sm::LabelOptions{.role = sm::TextRole::title});
  side->add<sm::Label>("Gallery, v0.1", sm::LabelOptions{.role = sm::TextRole::caption});
  side->add<sm::Spacer>(0.0f, 16.0f);
  for (const char* item : {"Controls", "Layout", "Motion", "Themes"}) {
    side->add<sm::Button>(item, sm::ButtonVariant::ghost)->set_align(sm::Align::stretch);
  }
  side->add<sm::Spacer>();
  side->add<sm::Label>("Theme", sm::LabelOptions{.role = sm::TextRole::caption});
  auto themes = side->add<sm::Column>(sm::StackOptions{.spacing = 8});
  const sm::Theme& current = app.theme();
  for (const sm::Theme* t : {&sm::theme_dark(), &sm::theme_black(), &sm::theme_light()}) {
    auto radio = themes->add<sm::RadioButton>(t->name, !file && *t == current);
    radio->on_select([&app, t] {
      app.stop_watching_theme();
      app.set_theme(*t);
    });
  }
  if (file) {
    auto radio = themes->add<sm::RadioButton>(file->name, true);
    radio->on_select([&app, path = file->path] { app.watch_theme(path); });
  }
}

inline void build_content(sm::App& app, sm::Ref<sm::Column> content) {
  content->add<sm::Label>("Gallery", sm::LabelOptions{.role = sm::TextRole::heading});
  content->add<sm::Label>(
      "Every control in v0.1. Try it with the keyboard too: Tab, Space, Enter and the arrows.",
      sm::LabelOptions{.role = sm::TextRole::secondary, .wrap = true});

  auto row = content->add<sm::Row>(sm::StackOptions{.spacing = 16, .align = sm::Align::start});
  auto buttons = titled_card(row, "Buttons");
  auto status = buttons->add<sm::Label>("Nothing pressed yet.",
                                        sm::LabelOptions{.role = sm::TextRole::secondary});
  auto button_row = buttons->add<sm::Row>(sm::StackOptions{.spacing = 8});
  for (auto variant : {sm::ButtonVariant::primary, sm::ButtonVariant::secondary,
                       sm::ButtonVariant::ghost, sm::ButtonVariant::danger}) {
    const char* names[] = {"Save", "Cancel", "Learn more", "Delete"};
    const std::string name = names[static_cast<int>(variant)];
    button_row->add<sm::Button>(name, variant)->on_click([status, name] {
      status->set_text(name + " pressed.");
    });
  }
  auto disabled = buttons->add<sm::Button>("Disabled", sm::ButtonVariant::primary);
  disabled->set_enabled(false);
  disabled->set_align(sm::Align::start);

  auto toggles = titled_card(row, "Toggles");
  toggles->add<sm::CheckBox>("Dithering", true);
  toggles->add<sm::CheckBox>("Reduce motion");
  toggles->add<sm::Switch>("Live reload", true);
  toggles->add<sm::Switch>("Telemetry");
  auto group = toggles->add<sm::Row>(sm::StackOptions{.spacing = 16});
  group->add<sm::RadioButton>("Daily", true);
  group->add<sm::RadioButton>("Weekly");
  group->add<sm::RadioButton>("Monthly");

  auto values = content->add<sm::Card>();
  values->add<sm::Label>("Values", sm::LabelOptions{.role = sm::TextRole::title});
  auto grid = values->add<sm::Grid>(sm::GridOptions{
      .columns = {sm::GridTrack::fixed(120), sm::GridTrack::stretch(), sm::GridTrack::fixed(48)}});
  grid->add<sm::Label>("Volume");
  auto slider =
      grid->add<sm::Slider>(sm::SliderOptions{.min = 0, .max = 100, .value = 62, .step = 1});
  auto readout = grid->add<sm::Label>("62", sm::LabelOptions{.role = sm::TextRole::secondary});
  grid->add<sm::Label>("Progress");
  auto progress = grid->add<sm::ProgressBar>(0.62f);
  grid->add<sm::Label>("");
  grid->add<sm::Label>("Working");
  auto busy = grid->add<sm::ProgressBar>();
  busy->set_indeterminate(true);
  slider->on_change([readout, progress](float v) {
    readout->set_text(std::to_string(static_cast<int>(std::lround(v))));
    progress->set_value(v / 100.0f);
  });

  content->add<sm::Separator>();
  auto actions = content->add<sm::Row>(sm::StackOptions{.spacing = 8});
  actions->add<sm::Button>("Open another window")->on_click([&app] {
    auto w = app.open_window({"Second window", {360, 200}});
    auto col = w->add<sm::Column>(sm::StackOptions{.spacing = 12, .padding = sm::Insets::all(24)});
    col->add<sm::Label>("A second window", sm::LabelOptions{.role = sm::TextRole::title});
    col->add<sm::Label>("Closing the last window ends the app.",
                        sm::LabelOptions{.role = sm::TextRole::secondary});
    col->add<sm::Button>("Close", sm::ButtonVariant::primary)->on_click([w] { w->close(); });
  });
  actions->add<sm::Spacer>();
  actions->add<sm::Label>("smalti v0.1", sm::LabelOptions{.role = sm::TextRole::caption});
}

}  // namespace gallery

namespace gallery {

// Fills `win` with the whole gallery.
inline void build(sm::App& app, sm::Ref<sm::Window> win, const FileTheme* file = nullptr) {
  auto root = win->add<sm::Row>();
  build_sidebar(app, root->add<Sidebar>(), file);
  auto content =
      root->add<sm::Column>(sm::StackOptions{.spacing = 20, .padding = sm::Insets::all(32)});
  content->set_stretch(1);
  build_content(app, content);
}

}  // namespace gallery
