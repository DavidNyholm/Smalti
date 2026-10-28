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
      cache.get(*fonts.regular, fonts.regular->glyph_index(static_cast<char32_t>(c)), static_cast<float>(size), 0, 1.0f);
    }
  }
  CHECK(cache.bytes() <= 64 * 1024);
  CHECK(cache.entries() > 0);
}
