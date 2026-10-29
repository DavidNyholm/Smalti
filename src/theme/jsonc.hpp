// A reader for JSON with comments and trailing commas (what VS Code calls jsonc), the format of
// theme files. Our own, so the library keeps no dependencies, and bounded, so a hostile file
// costs little: the limits below hold for any input.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace smalti::detail {

inline constexpr size_t kMaxJsonBytes = size_t{1} << 20;  // 1 MB: a theme is a few KB
inline constexpr int kMaxJsonDepth = 32;                  // objects and arrays inside each other
inline constexpr size_t kMaxJsonStringBytes = size_t{64} << 10;  // 64 KB per string
// Every value costs about a hundred bytes in the tree; this bounds the tree near 7 MB even for
// a 1 MB file of nothing but "0,".
inline constexpr size_t kMaxJsonValues = size_t{1} << 16;

// 1-based; the column counts characters (code points), as editors do, not bytes.
struct TextPos {
  int line = 1;
  int column = 1;
};

enum class JsonKind : uint8_t { null, boolean, number, string, array, object };

struct JsonMember;

struct JsonValue {
  JsonKind kind = JsonKind::null;
  TextPos pos;
  bool boolean = false;
  double number = 0;
  std::string string;
  std::vector<JsonValue> items;     // an array's values
  std::vector<JsonMember> members;  // an object's members, in file order, duplicates kept
};

struct JsonMember {
  std::string key;
  TextPos pos;  // of the key
  JsonValue value;
};

struct JsonError {
  TextPos pos;
  std::string message;
};

struct JsonParse {
  JsonValue root;
  std::optional<JsonError> error;  // the first syntax error; the tree is unusable when set
};

// Reads one value (any kind) and nothing else but whitespace and comments. A UTF-8 byte-order
// mark at the start is skipped. Never throws for bad input; reports it in `error`.
JsonParse parse_jsonc(std::string_view text);

const char* json_kind_name(JsonKind kind);  // "a number", "an object", ... for messages

}  // namespace smalti::detail
