// The gallery: every v0.1 control in one window. The interface itself is in gallery_ui.hpp,
// shared with the screenshot renderer.
//
//   gallery [theme.jsonc]    opens in that theme, listed after the built-in ones
//
// A theme file with errors is reported on stderr and the gallery opens in its usual theme;
// `smalti-theme check` shows the same errors in a terminal.
#include <cstdio>
#include <optional>

#include "gallery_ui.hpp"

int main(int argc, char** argv) {
  std::optional<sm::Theme> file_theme;
  if (argc > 1) {
    sm::ThemeLoad r = sm::load_theme(argv[1]);
    for (const sm::ThemeError& e : r.errors) {
      std::fprintf(stderr, "%s:%d:%d: %s\n", argv[1], e.line, e.column, e.message.c_str());
    }
    if (r.ok()) file_theme = std::move(r.theme);
  }
  sm::AppOptions options;
  if (file_theme) options.theme = &*file_theme;
  sm::App app(options);
  auto win = app.open_window({"smalti gallery", {1100, 720}});
  gallery::build(app, win, file_theme ? &*file_theme : nullptr);
  return app.run();
}
