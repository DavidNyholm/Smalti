// smalti-theme: checks theme files with the library's own loader, so it reports exactly what an
// application would.
//
//   smalti-theme check <file.jsonc> [more files]
//
// Prints each error as file:line:column: message (the form editors and terminals link to) and
// exits with 1 if any file has one, 2 if the command line is wrong.
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <smalti/theme.hpp>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

constexpr int kExitErrors = 1;
constexpr int kExitUsage = 2;

std::string utf8(const std::filesystem::path& p) {
  const std::u8string s = p.u8string();
  return std::string(s.begin(), s.end());
}

int usage() {
  std::fputs("usage: smalti-theme check <file.jsonc> [more files]\n", stderr);
  return kExitUsage;
}

bool check(const std::filesystem::path& file) {
  const smalti::ThemeLoad r = smalti::load_theme(file);
  const std::string name = utf8(file);
  if (r.ok()) {
    std::printf("%s: ok (\"%s\")\n", name.c_str(), r.theme.name.c_str());
    return true;
  }
  for (const smalti::ThemeError& e : r.errors) {
    if (e.line > 0) {
      std::printf("%s:%d:%d: %s\n", name.c_str(), e.line, e.column, e.message.c_str());
    } else {
      std::printf("%s: %s\n", name.c_str(), e.message.c_str());
    }
  }
  return false;
}

int run(std::string_view command, const std::vector<std::filesystem::path>& files) {
  if (command != "check" || files.empty()) return usage();
  bool all_ok = true;
  for (const std::filesystem::path& f : files) all_ok = check(f) && all_ok;
  return all_ok ? 0 : kExitErrors;
}

}  // namespace

#if defined(_WIN32)
// The wide entry point, so file names outside the system code page arrive intact; the output is
// UTF-8, like the messages.
int wmain(int argc, wchar_t** argv) {
  SetConsoleOutputCP(CP_UTF8);
  if (argc < 2) return usage();
  const std::wstring_view command = argv[1];
  return run(command == L"check" ? "check" : "", {argv + 2, argv + argc});
}
#else
int main(int argc, char** argv) {
  if (argc < 2) return usage();
  return run(argv[1], {argv + 2, argv + argc});
}
#endif
