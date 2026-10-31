// UTF-8 decoding, measuring and line wrapping.
#include <string>

#include "check.hpp"
#include "render/text.hpp"

using namespace smalti;
using namespace smalti::detail;

TEST(text_utf8_decoding_and_invalid_bytes) {
  const std::string s = "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80";  // a, e-acute, euro sign, an emoji
  size_t i = 0;
  CHECK_EQ(static_cast<uint32_t>(next_codepoint(s, i)), 0x61u);
  CHECK_EQ(static_cast<uint32_t>(next_codepoint(s, i)), 0xE9u);
  CHECK_EQ(static_cast<uint32_t>(next_codepoint(s, i)), 0x20ACu);
  CHECK_EQ(static_cast<uint32_t>(next_codepoint(s, i)), 0x1F600u);
  CHECK_EQ(i, s.size());
  const std::string bad = "\xFF\xC3";  // a stray byte, then a sequence cut short
  i = 0;
  CHECK_EQ(static_cast<uint32_t>(next_codepoint(bad, i)), 0xFFFDu);
  CHECK_EQ(static_cast<uint32_t>(next_codepoint(bad, i)), 0xFFFDu);
  CHECK_EQ(i, bad.size());
  const std::string interrupted =
      "\xC3"
      "A";  // a lead byte, then a plain letter instead of its continuation
  i = 0;
  CHECK_EQ(static_cast<uint32_t>(next_codepoint(interrupted, i)), 0xFFFDu);
  CHECK_EQ(i, size_t{1});  // only the lead byte is used up, so the letter survives
  if (i < interrupted.size())
    CHECK_EQ(static_cast<uint32_t>(next_codepoint(interrupted, i)), 0x41u);
}

TEST(text_width_grows_and_kerning_tightens) {
  const FontSet fonts = load_embedded_fonts();
  const Font& f = *fonts.regular;
  CHECK(text_width(f, 14, "") == 0.0f);
  CHECK(text_width(f, 14, "ab") > text_width(f, 14, "a"));
  CHECK_NEAR(text_width(f, 28, "Hello"), 2 * text_width(f, 14, "Hello"), 0.01);
  // AV kerns: narrower than A and V measured apart.
  CHECK(text_width(f, 14, "AV") < text_width(f, 14, "A") + text_width(f, 14, "V"));
}

TEST(text_wrap_breaks_at_spaces_and_inside_long_words) {
  const FontSet fonts = load_embedded_fonts();
  const Font& f = *fonts.regular;
  const float word = text_width(f, 14, "word");
  auto lines = wrap_lines(f, 14, "word word word", word * 2.5f);
  CHECK_EQ(lines.size(), size_t{2});
  CHECK_EQ(std::string(lines[0]), std::string("word word"));
  CHECK_EQ(std::string(lines[1]), std::string("word"));
  for (auto line : lines) CHECK(text_width(f, 14, line) <= word * 2.5f);

  lines = wrap_lines(f, 14, "abcdefghij", text_width(f, 14, "abcd"));
  CHECK(lines.size() >= 3);  // no spaces: broken between characters
  std::string joined;
  for (auto line : lines) joined += line;
  CHECK_EQ(joined, std::string("abcdefghij"));

  lines = wrap_lines(f, 14, "one\ntwo", 1000);
  CHECK_EQ(lines.size(), size_t{2});
  CHECK_EQ(std::string(lines[1]), std::string("two"));
  CHECK_EQ(wrap_lines(f, 14, "", 100).size(), size_t{1});
}

TEST(text_glyph_cache_respects_its_budget) {
  const FontSet fonts = load_embedded_fonts();
  GlyphCache cache(64 * 1024);
  for (int size = 10; size < 60; ++size) {
    for (char c = 'A'; c <= 'Z'; ++c) {
      cache.get(*fonts.regular, fonts.regular->glyph_index(static_cast<char32_t>(c)),
                static_cast<float>(size), 0, 1.0f);
    }
  }
  CHECK(cache.bytes() <= 64 * 1024);
  CHECK(cache.entries() > 0);
}

TEST(text_caret_stops_fall_where_glyphs_are_drawn) {
  const FontSet fonts = load_embedded_fonts();
  const Font& f = *fonts.regular;
  const auto empty = caret_stops(f, 14, "");
  CHECK_EQ(empty.size(), size_t{1});
  CHECK(empty[0].byte == 0 && empty[0].x == 0.0f);
  // One stop per character and one after the last, at character boundaries.
  const auto stops = caret_stops(f, 14, "a\xC3\xA9\xF0\x9F\x98\x80");
  CHECK_EQ(stops.size(), size_t{4});
  CHECK(stops[0].byte == 0 && stops[1].byte == 1 && stops[2].byte == 3 && stops[3].byte == 7);
  CHECK(stops[1].x > 0 && stops[2].x > stops[1].x && stops[3].x > stops[2].x);
  // The stop between a kerned pair is where the second glyph is drawn; the last is the width.
  const auto av = caret_stops(f, 14, "AV");
  CHECK(av[1].x < text_width(f, 14, "A"));
  CHECK_NEAR(av[2].x, text_width(f, 14, "AV"), 1e-4);
}
