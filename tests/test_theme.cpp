// Theme files: the built-in themes against the 0.1 values they replace, every value form, every
// error with its position, the base a file starts from, reading from disk, and damaged files.
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "check.hpp"
#include "smalti/theme.hpp"

namespace smalti::detail {
extern const unsigned char kThemeDark[];
extern const size_t kThemeDarkSize;
extern const unsigned char kThemeBlack[];
extern const size_t kThemeBlackSize;
extern const unsigned char kThemeLight[];
extern const size_t kThemeLightSize;
}  // namespace smalti::detail

using namespace smalti;
using namespace std::chrono_literals;

namespace {

bool contains(std::string_view s, std::string_view part) {
  return s.find(part) != std::string_view::npos;
}

ThemeError first_error(std::string_view text) {
  const ThemeLoad r = parse_theme(text);
  return r.errors.empty() ? ThemeError{-1, -1, "no error"} : r.errors.front();
}

constexpr Color white(float a) { return {1, 1, 1, a}; }
constexpr Color black(float a) { return {0, 0, 0, a}; }

// The built-in themes exactly as 0.1 defined them in code. The theme files must reproduce every
// field bit for bit, so the gallery draws the same pixels.
Theme reference_dark() {
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
}

Theme reference_black() {
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
  th.text = Color::hex(0xE3E3E6);
  th.text_secondary = Color::hex(0x9C9DA6);
  th.text_muted = Color::hex(0x63656E);
  th.accent = Color::hex(0x5585E8);
  th.accent_hover = Color::hex(0x6892EC);
  th.on_accent = Color::hex(0xFFFFFF);
  th.accent_soft = Color::hex(0x5585E8, 0.18f);
  th.success = Color::hex(0x35A86F);
  th.warning = Color::hex(0xD49A35);
  th.danger = Color::hex(0xDB4F46);
  th.shadows = false;
  th.shadow = black(0.0f);
  th.top_highlight = white(0.05f);
  th.dark = true;
  return th;
}

Theme reference_light() {
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
}

std::string_view builtin_text(int which) {
  const auto view = [](const unsigned char* data, size_t size) {
    return std::string_view(reinterpret_cast<const char*>(data), size);
  };
  switch (which) {
    case 0:
      return view(detail::kThemeDark, detail::kThemeDarkSize);
    case 1:
      return view(detail::kThemeBlack, detail::kThemeBlackSize);
    default:
      return view(detail::kThemeLight, detail::kThemeLightSize);
  }
}

bool is_builtin(const Theme& t) {
  return t == theme_dark() || t == theme_black() || t == theme_light();
}

}  // namespace

TEST(theme_builtin_files_equal_the_0_1_themes_field_for_field) {
  CHECK(theme_dark() == reference_dark());
  CHECK(theme_black() == reference_black());
  CHECK(theme_light() == reference_light());
  // And read through the public parser, they give the same themes.
  const ThemeLoad dark = parse_theme(builtin_text(0), "dark.jsonc");
  CHECK(dark.ok());
  CHECK(dark.theme == theme_dark());
}

TEST(theme_file_reads_every_value_form) {
  const ThemeLoad r = parse_theme(R"({
    "name": "Forms",
    "base": "dark",
    "tokens": {
      "accent": "#102030",
      "accent.hover": "#10203080",
      "accent.on": "#ffffff 50%",
      "accent.soft": "accent 25%",
      "state.hover": "accent.soft 50%",  // a reference to a reference: opacities multiply
      "radius.card": "radius.control",
      "radius.control": 4,
      "motion.base": "motion.fast",
      "motion.fast": "90ms",
      "depth.shadows": false,
      "text.snap": "depth.shadows",
      "text.blend": "srgb",
      "text.contrast": -0.25,
      "font.body": 15.5,
      "dark": false,
    },
    "widgets": {},
  })");
  CHECK(r.ok());
  const Theme& t = r.theme;
  CHECK_EQ(t.name, std::string("Forms"));
  CHECK(t.accent == Color::hex(0x102030));
  CHECK(t.accent_hover == Color::hex(0x102030, static_cast<float>(0x80) / 255.0f));
  CHECK(t.on_accent == white(0.5f));
  CHECK(t.accent_soft == Color::hex(0x102030, 0.25f));
  CHECK(t.hover == Color::hex(0x102030, 0.25f * 0.5f));
  CHECK_EQ(t.radius_card, 4.0f);
  CHECK(t.motion_base == 90ms);
  CHECK(!t.shadows);
  CHECK(!t.text_tuning.snap_x_height);
  CHECK(t.text_tuning.blend == TextBlend::srgb);
  CHECK_EQ(t.text_tuning.contrast, -0.25f);
  CHECK_EQ(t.text_body, 15.5f);
  CHECK(!t.dark);
  // What the file does not list comes from its base.
  CHECK(t.background == theme_dark().background);
  CHECK_EQ(t.text_title, theme_dark().text_title);
}

TEST(theme_file_changes_follow_through_the_base_references) {
  // Dark defines accent.soft as "accent 18%", so changing the accent changes it too.
  const ThemeLoad r = parse_theme(R"({"tokens": {"accent": "#ff0000"}})");
  CHECK(r.ok());
  CHECK(r.theme.accent_soft == Color::hex(0xFF0000, 0.18f));
  Theme expected = theme_dark();
  expected.name = "";
  expected.accent = Color::hex(0xFF0000);
  expected.accent_soft = Color::hex(0xFF0000, 0.18f);
  CHECK(r.theme == expected);
}

TEST(theme_file_base_and_name) {
  CHECK(parse_theme(R"({"base": "light"})").theme.background == theme_light().background);
  CHECK(parse_theme(R"({"base": "black"})").theme.raised == theme_black().raised);
  CHECK(parse_theme("{}").theme.raised == theme_dark().raised);
  CHECK_EQ(parse_theme("{}", "themes/night.jsonc").theme.name, std::string("night"));
  CHECK_EQ(parse_theme("{}", "C:\\themes\\night.v2.jsonc").theme.name, std::string("night.v2"));
  CHECK_EQ(parse_theme(R"({"name": "Nacht"})", "night.jsonc").theme.name, std::string("Nacht"));
  // "none" needs every token, and names the ones missing.
  const ThemeError e = first_error(R"({"base": "none", "tokens": {"accent": "#ffffff"}})");
  CHECK_EQ(e.column, 18);
  CHECK(contains(e.message, "needs every token"));
  CHECK(contains(e.message, "surface.titlebar"));
  CHECK(!contains(e.message, "accent,"));
}

TEST(theme_file_reports_each_error_where_it_is) {
  struct Case {
    const char* text;
    int line, column;
    const char* says;
  };
  const Case cases[] = {
      {R"([1])", 1, 1, "a theme is an object"},
      {R"({"tokenz": {}})", 1, 2, "unknown key \"tokenz\"; did you mean \"tokens\"?"},
      {R"({"name": 5})", 1, 10, "\"name\" is a string"},
      {R"({"base": "drak"})", 1, 10, "did you mean \"dark\"?"},
      {R"({"base": 1})", 1, 10, "\"base\" is \"dark\", \"black\", \"light\" or \"none\""},
      {R"({"tokens": []})", 1, 12, "\"tokens\" is an object"},
      {R"({"widgets": {"Button": {}}})", 1, 2, "does not read widget styles yet"},
      {R"({"tokens": {"accnet": "#ffffff"}})", 1, 13,
       "unknown token \"accnet\"; did you mean \"accent\"?"},
      {R"({"tokens": {"Accent": "#ffffff"}})", 1, 13, "did you mean \"accent\"?"},
      {R"({"tokens": {"accent": 5}})", 1, 23, "\"accent\" takes a colour"},
      {R"({"tokens": {"accent": ""}})", 1, 23, "got an empty string"},
      {R"({"tokens": {"accent": "radius.card"}})", 1, 23, "\"radius.card\" is a radius"},
      {R"({"tokens": {"accent": "acent"}})", 1, 23, "did you mean \"accent\"?"},
      {R"({"tokens": {"accent": "#12345"}})", 1, 23, "\"#12345\" is not a colour"},
      {R"({"tokens": {"accent": "#12345g"}})", 1, 23, "is not a colour"},
      {R"({"tokens": {"accent": "#123456 150%"}})", 1, 23, "an opacity from 0% to 100%"},
      {R"({"tokens": {"accent": "#123456 50"}})", 1, 23, "an opacity from 0% to 100%"},
      {R"({"tokens": {"accent": "#123456 nan%"}})", 1, 23, "an opacity from 0% to 100%"},
      {R"({"tokens": {"radius.card": 500}})", 1, 28, "a radius from 0 to 100"},
      {R"({"tokens": {"radius.card": "12"}})", 1, 28, "numbers are written without quotes"},
      {R"({"tokens": {"radius.card": "size.control"}})", 1, 28, "is a control size"},
      {R"({"tokens": {"size.control": 4}})", 1, 29,
       "from 16 to 96, or the name of another such token; got 4"},
      {R"({"tokens": {"font.body": 1000}})", 1, 26, "a font size from 6 to 96"},
      {R"({"tokens": {"text.contrast": -1}})", 1, 30, "from -0.5 to 2"},
      {R"({"tokens": {"motion.fast": 120}})", 1, 28, "whole milliseconds"},
      {R"({"tokens": {"motion.fast": "120 ms"}})", 1, 28, "whole milliseconds"},
      {R"({"tokens": {"motion.fast": "0.12s"}})", 1, 28, "whole milliseconds"},
      {R"({"tokens": {"motion.fast": "-5ms"}})", 1, 28, "got \"-5ms\""},
      {R"({"tokens": {"motion.fast": "99999ms"}})", 1, 28, "got \"99999ms\""},
      {R"({"tokens": {"motion.fast": "99999999999999999999ms"}})", 1, 28, "whole milliseconds"},
      {R"({"tokens": {"dark": "yes"}})", 1, 21, "unknown token \"yes\""},
      {R"({"tokens": {"dark": 1}})", 1, 21, "\"dark\" takes true or false"},
      {R"({"tokens": {"text.blend": "lienar"}})", 1, 27, "did you mean \"linear\"?"},
      {R"({"tokens": {"text.blend": "text.blend"}})", 1, 27, "loop: text.blend -> text.blend"},
      {"{\"tokens\": {\n  \"accent\": \"#ffffff\",\n  \"accent\": \"#000000\"\n}}", 3, 3,
       "\"accent\" is set twice; it was first set on line 2"},
      {"{\"tokens\": {\n    \"accent\": \"accent.soft\",\n    \"accent.soft\": \"accent 18%\"\n}}",
       2, 15, "loop: accent -> accent.soft -> accent"},
      {R"({"name": "x" "base": "dark"})", 1, 14, "expected ',' before this key"},
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

TEST(theme_file_reports_every_error_and_changes_nothing) {
  const ThemeLoad r = parse_theme(R"({
    "base": "light",
    "tokens": {"accnet": "#fff", "radius.card": 500, "accent": "#00ff00", "foo": 1}
  })");
  CHECK_EQ(r.errors.size(), size_t{3});
  // at(): a missing error fails the test instead of reading past the end.
  CHECK_EQ(r.errors.at(0).column, 16);
  CHECK(contains(r.errors.at(0).message, "accnet"));
  CHECK(contains(r.errors.at(1).message, "got 500"));
  CHECK(contains(r.errors.at(2).message, "\"foo\""));
  // The good "accent" in a bad file is not applied: the theme is the base, untouched.
  CHECK(r.theme == theme_light());
  // A file that does not parse at all leaves Dark.
  CHECK(parse_theme(R"({"base": "light",)").theme == theme_dark());
}

TEST(theme_file_errors_stop_at_a_hundred_and_count_the_rest) {
  std::string text = "{\"tokens\": {";
  for (int i = 0; i < 150; ++i) text += "\"unknown" + std::to_string(i) + "\": 1,";
  text += "}}";
  const ThemeLoad r = parse_theme(text);
  CHECK_EQ(r.errors.size(), size_t{101});
  CHECK_EQ(r.errors.back().line, 0);
  CHECK(contains(r.errors.back().message, "50 more errors"));
}

TEST(theme_load_reads_files_and_reports_ones_it_cannot) {
  namespace fs = std::filesystem;
  const fs::path dir =
      fs::temp_directory_path() /
      ("smalti_theme_test_" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(dir);
  const fs::path file = dir / fs::path(u8"\u00e5\u00e4\u00f6.jsonc");
  {
    std::ofstream out(file, std::ios::binary);
    out << R"({"base": "black", "tokens": {"accent": "#ff8800"}})";
  }
  const ThemeLoad good = load_theme(file);
  CHECK(good.ok());
  CHECK(good.theme.accent == Color::hex(0xFF8800));
  CHECK(good.theme.name == "\u00e5\u00e4\u00f6");

  const ThemeLoad missing = load_theme(dir / "missing.jsonc");
  CHECK_EQ(missing.errors.size(), size_t{1});
  CHECK_EQ(missing.errors[0].line, 0);
  CHECK(contains(missing.errors[0].message, "there is no file"));
  CHECK(missing.theme == theme_dark());

  const ThemeLoad folder = load_theme(dir);
  CHECK(contains(folder.errors.at(0).message, "is a folder"));

  {
    std::ofstream out(dir / "big.jsonc", std::ios::binary);
    out << std::string((size_t{1} << 20) + 1, ' ');
  }
  CHECK(contains(load_theme(dir / "big.jsonc").errors.at(0).message, "larger than 1 MB"));
  fs::remove_all(dir);
}

// Random damage to the built-in files, as the font reader gets: whatever comes back must be
// either a theme inside the documented ranges or errors that point inside the text, with an
// untouched built-in theme. Run under the sanitizers for memory errors.
TEST(theme_file_survives_damage) {
  constexpr int kRounds = 3000;
  constexpr int kMaxEdits = 4;
  const std::string_view structure = "{}[],:\"/*\\ #%.-0123456789eatrufnl";
  std::mt19937 rng(20261029);
  int parsed = 0;
  for (int round = 0; round < kRounds; ++round) {
    std::string text(builtin_text(round % 3));
    const int edits = 1 + static_cast<int>(rng() % kMaxEdits);
    for (int k = 0; k < edits && !text.empty(); ++k) {
      const size_t at = rng() % text.size();
      const size_t span = 1 + rng() % 16;
      switch (rng() % 6) {
        case 0:
          text[at] = static_cast<char>(rng());
          break;
        case 1:
          text.insert(at, 1, structure[rng() % structure.size()]);
          break;
        case 2:
          text.erase(at, span);
          break;
        case 3:
          text.insert(at, text.substr(at, span));
          break;
        case 4:
          text.resize(at);
          break;
        default:
          text[at] = structure[rng() % structure.size()];
          break;
      }
    }
    const ThemeLoad r = parse_theme(text);
    const int lines = 1 + static_cast<int>(std::count(text.begin(), text.end(), '\n'));
    for (const ThemeError& e : r.errors) {
      if (e.line < 0 || e.line > lines || e.column < 0 || e.message.empty()) {
        check::fail("an error outside the text: " + e.message, __FILE__, __LINE__);
      }
    }
    if (r.ok()) {
      ++parsed;
      const Theme& t = r.theme;
      CHECK(t.radius_control >= 0 && t.radius_control <= 100);
      CHECK(t.control_height >= 16 && t.control_height <= 96);
      CHECK(t.text_body >= 6 && t.text_body <= 96);
      CHECK(t.motion_base >= 0ms && t.motion_base <= 5000ms);
      CHECK(t.border.a >= 0 && t.border.a <= 1);
    } else {
      CHECK(is_builtin(r.theme));
    }
  }
  // Some damage lands in comments or whitespace and leaves a valid theme; most does not.
  CHECK(parsed > 0);
  CHECK(parsed < kRounds);
}
