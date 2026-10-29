// Live reload against real files: the system's own change reports (ReadDirectoryChangesW on
// Windows, inotify on Linux), read by the headless backend without blocking, and the reload
// that follows 50 ms after the last report.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "check.hpp"
#include "core/state.hpp"
#include "support.hpp"

using namespace smalti;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

// Reports can arrive a moment after the write (on Windows they come from the file system
// driver): wait this long for one before failing.
constexpr auto kReportTimeout = 3s;
// After the first report, how long to wait for the rest of one save's reports.
constexpr auto kSettle = 60ms;
// How long a save must go unreported to count as not watched. A rename is reported within a
// few milliseconds on both systems.
constexpr auto kQuietFor = 300ms;

const Color kRed = Color::hex(0xFF0000), kGreen = Color::hex(0x00FF00);

class TempFolder {
 public:
  TempFolder() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = fs::temp_directory_path() / ("smalti_reload_test_" + std::to_string(stamp));
    fs::create_directories(path_);
  }
  ~TempFolder() {
    std::error_code ignored;  // a leftover temporary folder is not worth failing a test over
    fs::remove_all(path_, ignored);
  }
  TempFolder(const TempFolder&) = delete;
  TempFolder& operator=(const TempFolder&) = delete;
  const fs::path& path() const { return path_; }

 private:
  fs::path path_;
};

void write(const fs::path& file, std::string_view text) {
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  out << text;
}

std::string accent_theme(std::string_view hex) {
  return R"({"tokens": {"accent": ")" + std::string(hex) + R"("}})";
}

bool reload_pending() { return detail::app_state().theme_reload_at != INT64_MAX; }

// Reads the watch until the system has reported a change, then lets the rest of that save's
// reports arrive. The fake clock does not move meanwhile, so nothing reloads yet.
bool wait_for_report(testing::Driver& d) {
  const auto give_up = std::chrono::steady_clock::now() + kReportTimeout;
  while (!reload_pending()) {
    if (std::chrono::steady_clock::now() > give_up) return false;
    std::this_thread::sleep_for(10ms);
    d.idle();
  }
  std::this_thread::sleep_for(kSettle);
  d.idle();
  return true;
}

}  // namespace

TEST(reload_applies_a_saved_theme_after_the_pause) {
  TempFolder dir;
  const fs::path file = dir.path() / "live.jsonc";
  write(file, accent_theme("#ff0000"));
  App app(headless());
  auto win = app.open_window({"t", {100, 100}});
  testing::Driver d(win);
  std::vector<std::string> errors;
  app.on_theme_error([&](std::span<const ThemeError> e) {
    for (const ThemeError& x : e) errors.push_back(x.message);
  });
  app.watch_theme(file);
  CHECK(app.theme().accent == kRed);  // applied at once
  CHECK_EQ(app.theme().name, std::string("live"));

  write(file, accent_theme("#00ff00"));
  CHECK(wait_for_report(d));
  d.advance(30ms);
  CHECK(app.theme().accent == kRed);  // still in the pause after the report
  d.advance(40ms);
  CHECK(app.theme().accent == kGreen);
  CHECK(!reload_pending());
  CHECK(errors.empty());
}

TEST(reload_keeps_the_last_good_theme_through_a_broken_save) {
  TempFolder dir;
  const fs::path file = dir.path() / "live.jsonc";
  write(file, accent_theme("#ff0000"));
  App app(headless());
  auto win = app.open_window({"t", {100, 100}});
  testing::Driver d(win);
  std::vector<ThemeError> errors;
  app.on_theme_error(
      [&](std::span<const ThemeError> e) { errors.insert(errors.end(), e.begin(), e.end()); });
  app.watch_theme(file);

  write(file, accent_theme("#ff00"));
  CHECK(wait_for_report(d));
  d.advance(100ms);
  CHECK(app.theme().accent == kRed);
  CHECK_EQ(errors.size(), size_t{1});
  CHECK(!errors.empty() && errors[0].line == 1 && errors[0].column == 23);

  write(file, accent_theme("#00ff00"));
  CHECK(wait_for_report(d));
  d.advance(100ms);
  CHECK(app.theme().accent == kGreen);
  CHECK_EQ(errors.size(), size_t{1});
}

TEST(reload_follows_a_save_that_renames_a_new_file_over_the_old) {
  // What most editors do: write the new text to a temporary file, then rename it over the
  // old one. A watch on the file itself would be lost with the old file.
  TempFolder dir;
  const fs::path file = dir.path() / "live.jsonc";
  write(file, accent_theme("#ff0000"));
  App app(headless());
  auto win = app.open_window({"t", {100, 100}});
  testing::Driver d(win);
  app.watch_theme(file);
  for (const std::string_view hex : {"#00ff00", "#0000ff"}) {
    const fs::path temp = dir.path() / "live.jsonc.tmp";
    write(temp, accent_theme(hex));
    fs::rename(temp, file);
    CHECK(wait_for_report(d));
    d.advance(100ms);
  }
  CHECK(app.theme().accent == Color::hex(0x0000FF));
}

TEST(reload_runs_once_for_a_burst_of_saves) {
  TempFolder dir;
  const fs::path file = dir.path() / "live.jsonc";
  write(file, accent_theme("#ff0000"));
  App app(headless());
  auto win = app.open_window({"t", {100, 100}});
  testing::Driver d(win);
  int reports = 0;
  app.on_theme_error([&](std::span<const ThemeError>) { ++reports; });
  app.watch_theme(file);
  for (int i = 0; i < 3; ++i) write(file, accent_theme("#bad"));  // invalid, so each load reports
  CHECK(wait_for_report(d));
  d.advance(100ms);
  CHECK_EQ(reports, 1);
}

TEST(reload_ignores_other_files_and_stops_when_asked) {
  TempFolder dir;
  const fs::path file = dir.path() / "live.jsonc";
  write(file, accent_theme("#ff0000"));
  App app(headless());
  auto win = app.open_window({"t", {100, 100}});
  testing::Driver d(win);
  app.watch_theme(file);

  // Saved the way editors save, by renaming a new file into place, which every system reports
  // at once (Windows reports a file rewritten in place only as its cache is written out).
  const auto save = [&](const fs::path& target, std::string_view text) {
    const fs::path temp = dir.path() / "save.tmp";
    write(temp, text);
    fs::rename(temp, target);
  };
  // Nothing is scheduled for a while after a save: no report for the watched file came in.
  const auto stays_quiet = [&] {
    for (auto waited = 0ms; waited < kQuietFor; waited += 10ms) {
      std::this_thread::sleep_for(10ms);
      d.idle();
      if (reload_pending()) return false;
    }
    return true;
  };

  save(dir.path() / "other.jsonc", accent_theme("#00ff00"));
  CHECK(stays_quiet());

  app.stop_watching_theme();
  save(file, accent_theme("#00ff00"));
  CHECK(stays_quiet());  // the folder is no longer watched: nothing even schedules a reload
  d.advance(200ms);
  CHECK(app.theme().accent == kRed);
}

TEST(reload_reports_a_folder_that_cannot_be_watched) {
  TempFolder dir;
  App app(headless());
  std::vector<std::string> errors;
  app.on_theme_error([&](std::span<const ThemeError> e) {
    for (const ThemeError& x : e) errors.push_back(x.message);
  });
  app.watch_theme(dir.path() / "missing" / "live.jsonc");
  CHECK_EQ(errors.size(), size_t{2});
  CHECK(!errors.empty() && errors[0].find("there is no file") != std::string::npos);
  CHECK(errors.size() > 1 && errors[1].find("cannot watch") != std::string::npos);
  CHECK(app.theme() == theme_dark());
}

TEST(reload_hands_back_an_exception_from_the_error_handler) {
  TempFolder dir;
  const fs::path file = dir.path() / "live.jsonc";
  write(file, accent_theme("#ff0000"));
  App app(headless());
  auto win = app.open_window({"t", {100, 100}});
  testing::Driver d(win);
  app.on_theme_error([](std::span<const ThemeError>) { throw std::runtime_error("handler"); });
  app.watch_theme(file);
  write(file, accent_theme("#bad"));
  CHECK(wait_for_report(d));
  bool rethrown = false;
  try {
    d.advance(100ms);
  } catch (const std::runtime_error& e) {
    rethrown = std::string(e.what()) == "handler";
  }
  CHECK(rethrown);
}
