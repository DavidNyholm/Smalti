// The gallery: every v0.1 control in one window. The interface itself is in gallery_ui.hpp,
// shared with the screenshot renderer.
//
//   gallery [theme.jsonc]    opens in that theme, and restyles itself each time it is saved
//
// Errors in the theme file are reported on stderr and the gallery keeps its last good theme;
// `smalti-theme check` shows the same errors in a terminal.
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>

#include "gallery_ui.hpp"

namespace {

std::string utf8(const std::filesystem::path& p) {
  const std::u8string s = p.u8string();
  return std::string(s.begin(), s.end());
}

int run_gallery(const std::optional<std::filesystem::path>& theme_file) {
  bool failed = false;  // declared before the App, whose error handler refers to it
  sm::App app;
  std::optional<gallery::FileTheme> file;
  if (theme_file) {
    const std::string shown = utf8(*theme_file);
    app.on_theme_error([&failed, shown](std::span<const sm::ThemeError> errors) {
      failed = true;
      for (const sm::ThemeError& e : errors) {
        std::fprintf(stderr, "%s:%d:%d: %s\n", shown.c_str(), e.line, e.column, e.message.c_str());
      }
    });
    app.watch_theme(*theme_file);
    const std::string stem = utf8(theme_file->stem());
    file = gallery::FileTheme{failed ? stem : app.theme().name, *theme_file};
  }
  auto win = app.open_window({"smalti gallery", {1100, 720}});
  gallery::build(app, win, file ? &*file : nullptr);
  return app.run();
}

}  // namespace

#if defined(_WIN32)
// The wide entry point, so a theme file's name outside the system code page arrives intact.
int wmain(int argc, wchar_t** argv) {
  return run_gallery(argc > 1 ? std::optional<std::filesystem::path>(argv[1]) : std::nullopt);
}
#else
int main(int argc, char** argv) {
  return run_gallery(argc > 1 ? std::optional<std::filesystem::path>(argv[1]) : std::nullopt);
}
#endif
