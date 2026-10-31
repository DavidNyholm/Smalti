// The editing model of TextBox: each operation on its own, then thousands of random operations
// against a deliberately simple reference that works on whole code points, compared after every
// step, and undo and redo taken all the way back and forth.
#include <algorithm>
#include <cstdio>
#include <random>
#include <string>

#include "check.hpp"
#include "platform/text_input.hpp"
#include "render/text.hpp"
#include "widgets/text_edit.hpp"

using smalti::detail::one_line;
using smalti::detail::TextEdit;
using Unit = TextEdit::Unit;

namespace {

const std::string kEAcute = "\xC3\xA9";         // 2 bytes
const std::string kSun = "\xE6\x97\xA5";        // a CJK character, 3 bytes
const std::string kSmile = "\xF0\x9F\x98\x80";  // an emoji, 4 bytes

TextEdit typed(std::string_view text, size_t max_chars = 0) {
  TextEdit e(max_chars);
  e.type(text);
  return e;
}

}  // namespace

TEST(text_edit_one_line_cleans_what_a_field_takes) {
  CHECK_EQ(one_line("a\nb"), std::string("a b"));
  CHECK_EQ(one_line("a\r\nb"), std::string("a b"));
  CHECK_EQ(one_line("\n\nab\n"), std::string("ab"));  // a copied line's own line break
  CHECK_EQ(one_line("a\tb"), std::string("a b"));
  CHECK_EQ(one_line("a\x01\x7F"
                    "b"),
           std::string("ab"));
  CHECK_EQ(one_line("a\xFF"), "a\xEF\xBF\xBD");
  CHECK_EQ(one_line(kSmile + kSun), kSmile + kSun);
}

TEST(text_edit_typing_moves_the_caret_by_characters) {
  TextEdit e = typed("h" + kEAcute + "j" + kSmile);
  CHECK_EQ(e.text(), "h" + kEAcute + "j" + kSmile);
  CHECK_EQ(e.caret(), size_t{8});
  e.move_left(Unit::character, false);
  CHECK_EQ(e.caret(), size_t{4});  // over the whole emoji
  e.move_left(Unit::character, false);
  e.move_left(Unit::character, false);
  CHECK_EQ(e.caret(), size_t{1});  // over the whole e-acute
  e.move_to(2, false);             // inside the e-acute: its start
  CHECK_EQ(e.caret(), size_t{1});
  e.move_right(Unit::character, true);
  CHECK_EQ(e.selected(), kEAcute);
  e.move_left(Unit::character, false);  // a selection collapses to its start
  CHECK_EQ(e.caret(), size_t{1});
  CHECK(!e.has_selection());
  e.move_home(false);
  e.move_left(Unit::character, false);
  CHECK_EQ(e.caret(), size_t{0});
  e.move_end(true);
  CHECK_EQ(e.anchor(), size_t{0});
  CHECK_EQ(e.caret(), e.text().size());
}

TEST(text_edit_words_as_windows_editors_have_them) {
  TextEdit e = typed("hello, world  " + kSun + kSun + " x_1");
  //                  0    5 7    12  14     20 21
  CHECK_EQ(e.word_right(0), size_t{5});    // past "hello"
  CHECK_EQ(e.word_right(5), size_t{7});    // past "," and its space
  CHECK_EQ(e.word_right(7), size_t{14});   // past "world" and two spaces
  CHECK_EQ(e.word_right(14), size_t{21});  // two CJK characters are one word
  CHECK_EQ(e.word_right(21), e.text().size());
  CHECK_EQ(e.word_left(e.text().size()), size_t{21});
  CHECK_EQ(e.word_left(21), size_t{14});
  CHECK_EQ(e.word_left(14), size_t{7});
  CHECK_EQ(e.word_left(7), size_t{5});
  CHECK_EQ(e.word_left(5), size_t{0});
  e.select_word(9);
  CHECK_EQ(e.selected(), std::string("world"));
  e.select_word(12);
  CHECK_EQ(e.selected(), std::string("  "));
  e.select_word(5);
  CHECK_EQ(e.selected(), std::string(","));
  e.select_word(e.text().size());  // at the end: the last word
  CHECK_EQ(e.selected(), std::string("x_1"));
  e.move_to(12, false);
  CHECK(e.erase_back(Unit::word));
  CHECK_EQ(e.text(), "hello,   " + kSun + kSun + " x_1");
  CHECK(e.erase_forward(Unit::word));
  CHECK_EQ(e.text(), "hello, " + kSun + kSun + " x_1");
}

TEST(text_edit_selection_is_replaced_and_erased) {
  TextEdit e = typed("abcdef");
  e.move_to(1, false);
  e.move_to(4, true);
  CHECK_EQ(e.selected(), std::string("bcd"));
  CHECK(e.type("X"));
  CHECK_EQ(e.text(), std::string("aXef"));
  e.select_all();
  CHECK(e.erase_back(Unit::character));
  CHECK_EQ(e.text(), std::string(""));
  CHECK(!e.erase_back(Unit::character));
  CHECK(!e.erase_forward(Unit::word));
  CHECK(!e.type("\n\x01"));  // only a line break and a control: nothing to type
}

TEST(text_edit_length_limit_counts_characters) {
  TextEdit e = typed("ab" + kSmile + "cd", 3);
  CHECK_EQ(e.text(), "ab" + kSmile);
  CHECK(!e.type("x"));
  e.move_to(1, false);
  e.move_to(2, true);  // "b" selected: room for one character
  CHECK(e.insert("xyz"));
  CHECK_EQ(e.text(), "ax" + kSmile);
  e.set_max_chars(2);
  CHECK_EQ(e.text(), std::string("ax"));
  CHECK(!e.undo());  // a new limit forgets the steps it could undo past
  e.set_text("12345");
  CHECK_EQ(e.text(), std::string("12"));
}

TEST(text_edit_undo_groups_typing_into_words) {
  TextEdit e;
  for (const char c : std::string("hello world")) e.type(std::string(1, c));
  CHECK(e.undo());
  CHECK_EQ(e.text(), std::string("hello "));  // the word typed after a space was its own step
  CHECK(e.undo());
  CHECK_EQ(e.text(), std::string(""));
  CHECK(!e.undo());
  CHECK(e.redo());
  CHECK(e.redo());
  CHECK_EQ(e.text(), std::string("hello world"));
  CHECK_EQ(e.caret(), size_t{11});
  CHECK(!e.redo());

  // A pause, or a caret move, ends a step.
  e.type("ab");
  e.break_step();
  e.type("cd");
  CHECK(e.undo());
  CHECK_EQ(e.text(), std::string("hello worldab"));
  e.move_left(Unit::character, false);
  e.move_right(Unit::character, false);
  e.type("x");
  e.type("y");
  CHECK(e.undo());
  CHECK_EQ(e.text(), std::string("hello worldab"));

  // Backspaces in a row are one step; a paste is one of its own; a new edit ends redo.
  e.erase_back(Unit::character);
  e.erase_back(Unit::character);
  CHECK_EQ(e.text(), std::string("hello world"));
  e.insert("!!");
  CHECK(e.undo());
  CHECK_EQ(e.text(), std::string("hello world"));
  CHECK(e.undo());
  CHECK_EQ(e.text(), std::string("hello worldab"));
  CHECK_EQ(e.caret(), size_t{13});
  e.type("z");
  CHECK(!e.redo());
}

TEST(text_edit_a_caret_move_ends_the_typing_step) {
  TextEdit e;
  e.type("a");
  e.type("b");
  e.move_left(Unit::character, false);
  e.move_right(Unit::character, false);  // back where it was
  e.type("c");
  CHECK(e.undo());
  CHECK_EQ(e.text(), std::string("ab"));
}

TEST(text_edit_set_text_starts_over) {
  TextEdit e = typed("abc");
  e.set_text("line one\nline two\n");
  CHECK_EQ(e.text(), std::string("line one line two"));
  CHECK_EQ(e.caret(), e.text().size());
  CHECK(!e.has_selection());
  CHECK(!e.undo());
}

// --- The reference model, on whole code points ---------------------------------------------

namespace {

enum class Kind { space, punctuation, word };

Kind kind_of(char32_t c) {
  if (c == ' ' || c == 0xA0 || c == 0x3000) return Kind::space;
  if (c >= 0x80 || c == '_' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
      (c >= 'A' && c <= 'Z'))
    return Kind::word;
  return Kind::punctuation;
}

std::u32string decode(std::string_view s) {
  std::u32string out;
  for (size_t i = 0; i < s.size();) out += smalti::detail::next_codepoint(s, i);
  return out;
}

std::string encode(const std::u32string& s) {
  std::string out;
  for (const char32_t c : s) out += smalti::detail::utf8_of(c);
  return out;
}

struct Reference {
  std::u32string text;
  size_t caret = 0, anchor = 0, max_chars = 0;

  size_t lo() const { return std::min(caret, anchor); }
  size_t hi() const { return std::max(caret, anchor); }
  size_t word_left(size_t p) const {
    while (p > 0 && kind_of(text[p - 1]) == Kind::space) --p;
    if (p == 0) return 0;
    const Kind k = kind_of(text[p - 1]);
    while (p > 0 && kind_of(text[p - 1]) == k) --p;
    return p;
  }
  size_t word_right(size_t p) const {
    if (p < text.size() && kind_of(text[p]) != Kind::space) {
      const Kind k = kind_of(text[p]);
      while (p < text.size() && kind_of(text[p]) == k) ++p;
    }
    while (p < text.size() && kind_of(text[p]) == Kind::space) ++p;
    return p;
  }
  void place(size_t p, bool extend) {
    caret = p;
    if (!extend) anchor = p;
  }
  bool insert(std::u32string add) {
    if (max_chars != 0) {
      const size_t kept = text.size() - (hi() - lo());
      add.resize(std::min(add.size(), kept < max_chars ? max_chars - kept : 0));
    }
    if (add.empty()) return false;
    const size_t at = lo();
    text.replace(at, hi() - at, add);
    caret = anchor = at + add.size();
    return true;
  }
  bool erase(size_t from, size_t to) {
    text.erase(from, to - from);
    caret = anchor = from;
    return true;
  }
  bool erase_back(bool word) {
    if (caret != anchor) return erase(lo(), hi());
    if (caret == 0) return false;
    return erase(word ? word_left(caret) : caret - 1, caret);
  }
  bool erase_forward(bool word) {
    if (caret != anchor) return erase(lo(), hi());
    if (caret == text.size()) return false;
    return erase(caret, word ? word_right(caret) : caret + 1);
  }
  void move_left(bool word, bool extend) {
    if (!word && caret != anchor && !extend) return place(lo(), false);
    place(word ? word_left(caret) : (caret > 0 ? caret - 1 : 0), extend);
  }
  void move_right(bool word, bool extend) {
    if (!word && caret != anchor && !extend) return place(hi(), false);
    place(word ? word_right(caret) : std::min(caret + 1, text.size()), extend);
  }
  void select_word(size_t p) {
    place(p, false);
    if (text.empty()) return;
    if (p == text.size()) --p;
    const Kind k = kind_of(text[p]);
    size_t start = p, end = p + 1;
    while (start > 0 && kind_of(text[start - 1]) == k) --start;
    while (end < text.size() && kind_of(text[end]) == k) ++end;
    anchor = start;
    caret = end;
  }
};

// The byte offset of character `index` in UTF-8 `s`.
size_t byte_of(const std::u32string& s, size_t index) { return encode(s.substr(0, index)).size(); }

// The character index a byte offset falls in, rounded down to the character's start.
size_t index_of(const std::string& s, size_t byte) {
  size_t index = 0;
  for (size_t i = 0; i < byte && i < s.size(); ++i)
    index += (static_cast<uint8_t>(s[i]) & 0xC0) != 0x80;
  return index - ((byte < s.size() && (static_cast<uint8_t>(s[byte]) & 0xC0) == 0x80) ? 1 : 0);
}

bool same(const TextEdit& e, const Reference& r) {
  return e.text() == encode(r.text) && e.caret() == byte_of(r.text, r.caret) &&
         e.anchor() == byte_of(r.text, r.anchor);
}

// One random edit or move on both models; false if they disagree about whether it changed text.
bool step(std::mt19937& rng, TextEdit& e, Reference& r) {
  static const std::string pieces[] = {"a", "b", "Z", "1",     " ",  " ",
                                       ".", ",", "_", kEAcute, kSun, kSmile};
  const auto pick = [&](int n) { return static_cast<int>(rng() % static_cast<unsigned>(n)); };
  const bool extend = pick(2) == 0;
  const bool word = pick(3) == 0;
  switch (pick(12)) {
    case 0:
    case 1: {
      std::string s;
      for (int n = 1 + pick(3); n > 0; --n) s += pieces[pick(static_cast<int>(std::size(pieces)))];
      const bool typing = pick(4) != 0;
      return (typing ? e.type(s) : e.insert(s)) == r.insert(decode(s));
    }
    case 2:
      return e.erase_back(word ? Unit::word : Unit::character) == r.erase_back(word);
    case 3:
      return e.erase_forward(word ? Unit::word : Unit::character) == r.erase_forward(word);
    case 4:
      e.move_left(word ? Unit::word : Unit::character, extend);
      r.move_left(word, extend);
      return true;
    case 5:
      e.move_right(word ? Unit::word : Unit::character, extend);
      r.move_right(word, extend);
      return true;
    case 6: {
      const size_t byte = static_cast<size_t>(pick(static_cast<int>(e.text().size()) + 1));
      e.move_to(byte, extend);
      r.place(index_of(e.text(), byte), extend);
      return true;
    }
    case 7: {
      const size_t byte = static_cast<size_t>(pick(static_cast<int>(e.text().size()) + 1));
      e.select_word(byte);
      r.select_word(index_of(e.text(), byte));
      return true;
    }
    case 8:
      if (pick(2) == 0) {
        e.move_home(extend);
        r.place(0, extend);
      } else {
        e.move_end(extend);
        r.place(r.text.size(), extend);
      }
      return true;
    case 9:
      e.select_all();
      r.anchor = 0;
      r.caret = r.text.size();
      return true;
    case 10:
      return e.erase_selection() == (r.caret != r.anchor && r.erase(r.lo(), r.hi()));
    default:
      e.break_step();
      return true;
  }
}

}  // namespace

TEST(text_edit_random_operations_match_the_reference) {
  constexpr int kRounds = 40;
  constexpr int kStepsPerRound = 300;  // under the undo history's length
  for (int round = 0; round < kRounds; ++round) {
    std::mt19937 rng(static_cast<unsigned>(round));
    const size_t limit = round % 4 == 0 ? 12 : 0;
    TextEdit e(limit);
    Reference r;
    r.max_chars = limit;
    for (int i = 0; i < kStepsPerRound; ++i) {
      const bool agreed = step(rng, e, r);
      if (!agreed || !same(e, r)) {
        CHECK(false);
        std::printf("    round %d, step %d: model \"%s\" caret %zu anchor %zu\n", round, i,
                    e.text().c_str(), e.caret(), e.anchor());
        return;
      }
    }
    // Undo takes every edit back, and redo brings every one again.
    const std::string final_text = e.text();
    while (e.undo()) {
    }
    CHECK_EQ(e.text(), std::string(""));
    while (e.redo()) {
    }
    CHECK_EQ(e.text(), final_text);
  }
}
