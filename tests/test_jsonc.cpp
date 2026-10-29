// The jsonc reader on its own: every value form, comments and trailing commas, and the position
// and wording of every syntax error. Errors as a theme file reports them are in test_theme.cpp.
#include <string>

#include "check.hpp"
#include "theme/jsonc.hpp"

using namespace smalti::detail;

namespace {

bool contains(std::string_view s, std::string_view part) {
  return s.find(part) != std::string_view::npos;
}

// The syntax error in `text`, or a position of -1 if it has none.
JsonError error_in(std::string_view text) {
  JsonParse p = parse_jsonc(text);
  return p.error ? *p.error : JsonError{{-1, -1}, "no error"};
}

}  // namespace

TEST(jsonc_reads_every_kind_of_value) {
  const JsonParse p = parse_jsonc(R"({
    "s": "a\"b\\c\/d\b\f\n\r\t\u00e9\ud83d\ude00",
    "n": [0, -0.5, 1e2, 1.5E-3, 12],
    "b": [true, false, null],
    "o": {"inner": {}},
    "e": []
  })");
  CHECK(!p.error);
  const JsonValue& root = p.root;
  CHECK(root.kind == JsonKind::object);
  CHECK_EQ(root.members.size(), size_t{5});
  CHECK_EQ(root.members[0].key, std::string("s"));
  CHECK_EQ(root.members[0].value.string,
           std::string("a\"b\\c/d\b\f\n\r\t\u00e9") + "\xF0\x9F\x98\x80");
  const JsonValue& n = root.members[1].value;
  CHECK_EQ(n.items.size(), size_t{5});
  CHECK_EQ(n.items[1].number, -0.5);
  CHECK_EQ(n.items[2].number, 100.0);
  CHECK_EQ(n.items[3].number, 0.0015);
  const JsonValue& b = root.members[2].value;
  CHECK(b.items[0].kind == JsonKind::boolean && b.items[0].boolean);
  CHECK(b.items[1].kind == JsonKind::boolean && !b.items[1].boolean);
  CHECK(b.items[2].kind == JsonKind::null);
  CHECK(root.members[3].value.members[0].value.kind == JsonKind::object);
  CHECK(root.members[4].value.kind == JsonKind::array && root.members[4].value.items.empty());
}

TEST(jsonc_takes_comments_and_trailing_commas_and_keeps_positions) {
  const JsonParse p = parse_jsonc(
      "// a theme\n"
      "{ /* before */ \"a\" /* between */ : 1, // after\n"
      "  \"b\": [1, 2,],\n"
      "}\n");
  CHECK(!p.error);
  CHECK_EQ(p.root.members.size(), size_t{2});
  CHECK_EQ(p.root.pos.line, 2);
  CHECK_EQ(p.root.members[0].pos.column, 16);
  CHECK_EQ(p.root.members[1].pos.line, 3);
  CHECK_EQ(p.root.members[1].pos.column, 3);
  CHECK_EQ(p.root.members[1].value.items.size(), size_t{2});
}

TEST(jsonc_keeps_duplicate_keys_for_the_caller_to_judge) {
  const JsonParse p = parse_jsonc(R"({"a": 1, "a": 2})");
  CHECK(!p.error);
  CHECK_EQ(p.root.members.size(), size_t{2});
}

TEST(jsonc_columns_count_characters_and_skip_a_byte_order_mark) {
  // Two two-byte characters before the second key: column 16, not byte 18.
  JsonParse p = parse_jsonc("{\"name\": \"\u00e9\u00e9\", \"x\": 1}");
  CHECK(!p.error);
  CHECK_EQ(p.root.members[1].pos.column, 16);
  p = parse_jsonc("\xEF\xBB\xBF{\"x\": 1}");
  CHECK(!p.error);
  CHECK_EQ(p.root.members[0].pos.column, 2);
  p = parse_jsonc("{\r\n\"x\": 1}");
  CHECK_EQ(p.root.members[0].pos.line, 2);
  CHECK_EQ(p.root.members[0].pos.column, 1);
}

TEST(jsonc_reports_where_the_syntax_breaks) {
  struct Case {
    const char* text;
    int line, column;
    const char* says;
  };
  const Case cases[] = {
      {R"({"name": "x" "base": "dark"})", 1, 14, "expected ',' before this key"},
      {R"({"a": 1 ]})", 1, 9, "expected ',' or '}'"},
      {R"([1 2])", 1, 4, "expected ',' or ']'"},
      {R"({"name": "x})", 1, 10, "never ends"},
      {"{\"name\": \"x\n\"}", 1, 10, "not closed before the end of the line"},
      {"{\"name\": \"a\tb\"}", 1, 12, "control character"},
      {"{ /* x", 1, 3, "comment never ends"},
      {R"({"name": "a\qb"})", 1, 12, "unknown escape"},
      {R"({"name": "\u12"})", 1, 11, "four hexadecimal digits"},
      {R"({"name": "\ud800"})", 1, 11, "high surrogate"},
      {R"({"name": "\ud800A"})", 1, 11, "high surrogate"},
      {R"({"name": "\udc00"})", 1, 11, "low surrogate"},
      {R"({"a": 07})", 1, 7, "does not start with 0"},
      {R"({"a": -})", 1, 8, "digit after '-'"},
      {R"({"a": 1.})", 1, 9, "after the decimal point"},
      {R"({"a": 1e})", 1, 9, "in the exponent"},
      {R"({"a": 12px})", 1, 9, "\"px\" after the number"},
      {R"({"a": 1e999})", 1, 7, "out of range"},
      {R"({} x)", 1, 4, "after the closing '}'"},
      {R"({'name': 1})", 1, 2, "double quotes, not single quotes"},
      {R"({"a": 'x'})", 1, 7, "double quotes, not single quotes"},
      {R"({name: 1})", 1, 2, "keys are written in double quotes: found \"name\""},
      {R"({"a": blue})", 1, 7, "found \"blue\"; strings and token names"},
      {R"({"a": trueish})", 1, 7, "found \"trueish\""},
      {R"({"a" 1})", 1, 6, "expected ':' after the key"},
      {R"({,})", 1, 2, "expected a key"},
      {R"({"a": })", 1, 7, "expected a value, found '}'"},
      {"", 1, 1, "holds no value"},
      {"  // only a comment\n", 2, 1, "holds no value"},
      {"{\"name\": \"\xff\"}", 1, 11, "not valid UTF-8"},
      {"{\"name\": \"\xC0\xAF\"}", 1, 11, "not valid UTF-8"},          // overlong '/'
      {"{\"name\": \"\xED\xA0\x80\"}", 1, 11, "not valid UTF-8"},      // a surrogate
      {"{\"name\": \"\xF4\x90\x80\x80\"}", 1, 11, "not valid UTF-8"},  // beyond U+10FFFF
      {"{\"name\": \"\xE2\x82\"}", 1, 11, "not valid UTF-8"},          // cut short
  };
  for (const Case& c : cases) {
    const JsonError e = error_in(c.text);
    if (e.pos.line != c.line || e.pos.column != c.column || !contains(e.message, c.says)) {
      check::fail(std::string(c.text) + "  ->  " + std::to_string(e.pos.line) + ":" +
                      std::to_string(e.pos.column) + " " + e.message,
                  __FILE__, __LINE__);
    }
  }
}

TEST(jsonc_limits_hold_for_hostile_input) {
  // Nesting: the 33rd bracket is one too deep, for arrays and for objects, which have a check of
  // their own.
  JsonError e = error_in(std::string(40, '[') + std::string(40, ']'));
  CHECK_EQ(e.pos.column, kMaxJsonDepth + 1);
  CHECK(contains(e.message, "nested more than 32 deep"));
  std::string objects;
  for (int i = 0; i < 40; ++i) objects += "{\"a\":";
  objects += "1" + std::string(40, '}');
  e = error_in(objects);
  constexpr int kObjectOpenerWidth = 5;  // {"a":
  CHECK_EQ(e.pos.column, kMaxJsonDepth * kObjectOpenerWidth + 1);
  CHECK(contains(e.message, "nested more than 32 deep"));
  // Exactly 32 deep is fine.
  CHECK(!parse_jsonc(std::string(kMaxJsonDepth, '[') + std::string(kMaxJsonDepth, ']')).error);
  // A string over 64 KB is reported at its start.
  e = error_in("{\"name\": \"" + std::string(kMaxJsonStringBytes + 1, 'a') + "\"}");
  CHECK_EQ(e.pos.column, 10);
  CHECK(contains(e.message, "longer than 64 KB"));
  // A string of exactly 64 KB is fine.
  CHECK(!parse_jsonc("\"" + std::string(kMaxJsonStringBytes, 'a') + "\"").error);
  // A file over 1 MB is refused before it is read.
  e = error_in(std::string(kMaxJsonBytes + 1, ' '));
  CHECK_EQ(e.pos.line, 1);
  CHECK(contains(e.message, "larger than 1 MB"));
  // Many small values: refused once there are more than the tree may hold.
  std::string many = "[";
  for (size_t i = 0; i < kMaxJsonValues + 10; ++i) many += "0,";
  many += "]";
  e = error_in(many);
  CHECK(contains(e.message, "more than 65536 values"));
}
