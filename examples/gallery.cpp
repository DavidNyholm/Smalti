// The gallery: every built-in control in one window. The interface itself is in gallery_ui.hpp,
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

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "gallery_ui.hpp"

namespace {

std::string utf8(const std::filesystem::path& p) {
  const std::u8string s = p.u8string();
  return std::string(s.begin(), s.end());
}

int run_gallery(const std::optional<std::filesystem::path>& theme_file) {
  sm::App app;
  std::optional<gallery::FileTheme> file;
  if (theme_file) {
    const std::string shown = utf8(*theme_file);
    app.on_theme_error([shown](std::span<const sm::ThemeError> errors) {
      for (const sm::ThemeError& e : errors) {
        std::fprintf(stderr, "%s:%d:%d: %s\n", shown.c_str(), e.line, e.column, e.message.c_str());
      }
    });
    app.watch_theme(*theme_file);
    // Listed by its file name: a file copied from a built-in theme keeps that theme's name.
    file = gallery::FileTheme{utf8(theme_file->filename()), *theme_file};
  }
  auto win = app.open_window({"smalti gallery", {1100, 720}});
  gallery::build(app, win, file ? &*file : nullptr);
  return app.run();
}

}  // namespace

#if defined(_WIN32)
namespace {

// A windowed program gets no console, so a theme's errors would go nowhere. Started from a
// terminal, it writes them to that terminal, unless they were already sent somewhere (2> file).
// Started from Explorer there is no terminal, and nothing to do.
void report_to_starting_terminal() {
  const HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
  if (err != nullptr && err != INVALID_HANDLE_VALUE) return;
  if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
  FILE* reopened = nullptr;
  if (freopen_s(&reopened, "CONOUT$", "w", stderr) != 0) {
    FreeConsole();  // attached but unwritable: errors stay unshown, as without a terminal
  }
}

}  // namespace

// The wide entry point, so a theme file's name outside the system code page arrives intact.
int wmain(int argc, wchar_t** argv) {
  report_to_starting_terminal();
  return run_gallery(argc > 1 ? std::optional<std::filesystem::path>(argv[1]) : std::nullopt);
}
#else
int main(int argc, char** argv) {
  return run_gallery(argc > 1 ? std::optional<std::filesystem::path>(argv[1]) : std::nullopt);
}
#endif
