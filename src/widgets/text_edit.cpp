// The editing model of a one-line text field (see text_edit.hpp).
#include "widgets/text_edit.hpp"

#include <algorithm>
#include <utility>

#include "platform/text_input.hpp"
#include "render/text.hpp"

namespace smalti::detail {

namespace {

constexpr size_t kMaxUndoSteps = 1000;  // older steps are forgotten

bool continuation(char c) { return (static_cast<uint8_t>(c) & 0xC0) == 0x80; }

bool is_control(char32_t cp) { return cp < 0x20 || (cp >= 0x7F && cp <= 0x9F); }

// Word moves stop where the kind of character changes.
enum class CharKind : uint8_t { space, punctuation, word };

CharKind kind_of(char32_t cp) {
  constexpr char32_t kNoBreakSpace = 0xA0, kIdeographicSpace = 0x3000;
  if (cp == ' ' || cp == kNoBreakSpace || cp == kIdeographicSpace) return CharKind::space;
  if (cp >= 0x80) return CharKind::word;  // any other character beyond ASCII
  const bool alnum =
      (cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z');
  return alnum || cp == '_' ? CharKind::word : CharKind::punctuation;
}

size_t count_chars(std::string_view s) {
  return static_cast<size_t>(
      std::count_if(s.begin(), s.end(), [](char c) { return !continuation(c); }));
}

// The first `n` characters of `s`.
std::string_view first_chars(std::string_view s, size_t n) {
  size_t i = 0, k = 0;
  for (; i < s.size(); ++i) {
    if (continuation(s[i])) continue;
    if (k == n) break;
    ++k;
  }
  return s.substr(0, i);
}

}  // namespace

std::string one_line(std::string_view utf8) {
  const std::string plain = plain_text(utf8);  // valid UTF-8, "\n" for every line break
  std::string_view s = plain;
  while (!s.empty() && s.front() == '\n') s.remove_prefix(1);
  while (!s.empty() && s.back() == '\n') s.remove_suffix(1);
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    const size_t start = i;
    const char32_t cp = next_codepoint(s, i);
    if (cp == '\n' || cp == '\t') {
      out += ' ';
    } else if (!is_control(cp)) {
      out.append(s.substr(start, i - start));
    }
  }
  return out;
}

std::string_view TextEdit::selected() const {
  return std::string_view(text_).substr(selection_start(), selection_end() - selection_start());
}

void TextEdit::set_text(std::string_view utf8) {
  std::string line = one_line(utf8);
  if (max_chars_ != 0) line.resize(first_chars(line, max_chars_).size());
  text_ = std::move(line);
  caret_ = anchor_ = text_.size();
  undo_.clear();
  redo_.clear();
  open_ = false;
}

void TextEdit::set_max_chars(size_t max_chars) {
  max_chars_ = max_chars;
  // A step undone under the new limit could bring back more than it allows.
  undo_.clear();
  redo_.clear();
  open_ = false;
  if (max_chars_ == 0) return;
  text_.resize(first_chars(text_, max_chars_).size());
  caret_ = std::min(caret_, text_.size());
  anchor_ = std::min(anchor_, text_.size());
}

bool TextEdit::type(std::string_view utf8) {
  const std::string added = fit(one_line(utf8));
  if (added.empty()) return false;  // nothing to type, or no room for it
  replace(selection_start(), selection_end() - selection_start(), added, StepKind::typing);
  return true;
}

bool TextEdit::insert(std::string_view utf8) {
  const std::string added = fit(one_line(utf8));
  if (added.empty()) return false;
  replace(selection_start(), selection_end() - selection_start(), added, StepKind::other);
  return true;
}

bool TextEdit::erase_back(Unit unit) {
  if (has_selection()) return erase_selection();
  if (caret_ == 0) return false;
  const size_t from = unit == Unit::word ? word_left(caret_) : prev(caret_);
  replace(from, caret_ - from, {},
          unit == Unit::character ? StepKind::erasing_back : StepKind::other);
  return true;
}

bool TextEdit::erase_forward(Unit unit) {
  if (has_selection()) return erase_selection();
  if (caret_ == text_.size()) return false;
  const size_t to = unit == Unit::word ? word_right(caret_) : next(caret_);
  replace(caret_, to - caret_, {},
          unit == Unit::character ? StepKind::erasing_forward : StepKind::other);
  return true;
}

bool TextEdit::erase_selection() {
  if (!has_selection()) return false;
  replace(selection_start(), selection_end() - selection_start(), {}, StepKind::other);
  return true;
}

void TextEdit::move_left(Unit unit, bool extend) {
  if (unit == Unit::character && has_selection() && !extend) {
    place(selection_start(), false);  // the selection collapses to its start
    return;
  }
  if (unit == Unit::word) {
    place(word_left(caret_), extend);
  } else {
    place(caret_ > 0 ? prev(caret_) : 0, extend);
  }
}

void TextEdit::move_right(Unit unit, bool extend) {
  if (unit == Unit::character && has_selection() && !extend) {
    place(selection_end(), false);
    return;
  }
  if (unit == Unit::word) {
    place(word_right(caret_), extend);
  } else {
    place(caret_ < text_.size() ? next(caret_) : caret_, extend);
  }
}

void TextEdit::move_home(bool extend) { place(0, extend); }

void TextEdit::move_end(bool extend) { place(text_.size(), extend); }

void TextEdit::move_to(size_t byte, bool extend) {
  byte = std::min(byte, text_.size());
  while (byte > 0 && byte < text_.size() && continuation(text_[byte])) --byte;
  place(byte, extend);
}

void TextEdit::select_all() {
  anchor_ = 0;
  caret_ = text_.size();
  open_ = false;
}

void TextEdit::select_word(size_t byte) {
  move_to(byte, false);
  if (text_.empty()) return;
  const size_t pos = caret_ == text_.size() ? prev(caret_) : caret_;
  const auto kind_at = [&](size_t b) {
    size_t i = b;
    return kind_of(next_codepoint(text_, i));
  };
  const CharKind kind = kind_at(pos);
  size_t start = pos, end = next(pos);
  while (start > 0 && kind_at(prev(start)) == kind) start = prev(start);
  while (end < text_.size() && kind_at(end) == kind) end = next(end);
  anchor_ = start;
  caret_ = end;
}

bool TextEdit::undo() {
  if (undo_.empty()) return false;
  Step s = std::move(undo_.back());
  undo_.pop_back();
  text_.replace(s.at, s.added.size(), s.removed);
  caret_ = s.caret_before;
  anchor_ = s.anchor_before;
  redo_.push_back(std::move(s));
  open_ = false;
  return true;
}

bool TextEdit::redo() {
  if (redo_.empty()) return false;
  Step s = std::move(redo_.back());
  redo_.pop_back();
  text_.replace(s.at, s.removed.size(), s.added);
  caret_ = s.caret_after;
  anchor_ = s.anchor_after;
  undo_.push_back(std::move(s));
  open_ = false;
  return true;
}

size_t TextEdit::word_left(size_t byte) const {
  const auto kind_before = [&](size_t b) {
    size_t i = prev(b);
    return kind_of(next_codepoint(text_, i));
  };
  size_t pos = std::min(byte, text_.size());
  while (pos > 0 && kind_before(pos) == CharKind::space) pos = prev(pos);
  if (pos == 0) return 0;
  const CharKind kind = kind_before(pos);
  while (pos > 0 && kind_before(pos) == kind) pos = prev(pos);
  return pos;
}

size_t TextEdit::word_right(size_t byte) const {
  const auto kind_at = [&](size_t b) {
    size_t i = b;
    return kind_of(next_codepoint(text_, i));
  };
  size_t pos = std::min(byte, text_.size());
  if (pos < text_.size() && kind_at(pos) != CharKind::space) {
    const CharKind kind = kind_at(pos);
    while (pos < text_.size() && kind_at(pos) == kind) pos = next(pos);
  }
  while (pos < text_.size() && kind_at(pos) == CharKind::space) pos = next(pos);
  return pos;
}

void TextEdit::replace(size_t at, size_t length, std::string_view added, StepKind kind) {
  Step step;
  step.at = at;
  step.removed = text_.substr(at, length);
  step.added = added;
  step.caret_before = caret_;
  step.anchor_before = anchor_;
  step.caret_after = step.anchor_after = at + added.size();
  step.kind = kind;
  text_.replace(at, length, added);
  caret_ = anchor_ = step.caret_after;
  redo_.clear();
  if (!merge(step)) {
    if (undo_.size() == kMaxUndoSteps) undo_.erase(undo_.begin());
    undo_.push_back(std::move(step));
  }
  open_ = kind != StepKind::other;
}

// Typing at the end of the last typing step, or erasing next to the last erasing step, adds to
// that step while it is open.
bool TextEdit::merge(const Step& step) {
  if (!open_ || undo_.empty() || undo_.back().kind != step.kind) return false;
  Step& last = undo_.back();
  switch (step.kind) {
    case StepKind::typing: {
      if (!step.removed.empty() || step.at != last.at + last.added.size()) return false;
      // A word typed after a space starts a step of its own. (The step adds to the last one,
      // so something stands before it.)
      size_t first = 0, before = prev(step.at);
      if (kind_of(next_codepoint(step.added, first)) != CharKind::space &&
          kind_of(next_codepoint(text_, before)) == CharKind::space) {
        return false;
      }
      last.added += step.added;
      break;
    }
    case StepKind::erasing_back:
      if (step.at + step.removed.size() != last.at) return false;
      last.at = step.at;
      last.removed.insert(0, step.removed);
      break;
    case StepKind::erasing_forward:
      if (step.at != last.at) return false;
      last.removed += step.removed;
      break;
    case StepKind::other:
      return false;
  }
  last.caret_after = step.caret_after;
  last.anchor_after = step.anchor_after;
  return true;
}

std::string TextEdit::fit(std::string_view added) const {
  if (max_chars_ == 0) return std::string(added);
  const size_t kept = count_chars(text_) - count_chars(selected());
  const size_t room = kept < max_chars_ ? max_chars_ - kept : 0;
  return std::string(first_chars(added, room));
}

size_t TextEdit::next(size_t byte) const {
  size_t i = byte + 1;
  while (i < text_.size() && continuation(text_[i])) ++i;
  return i;
}

size_t TextEdit::prev(size_t byte) const {
  size_t i = byte - 1;
  while (i > 0 && continuation(text_[i])) --i;
  return i;
}

void TextEdit::place(size_t caret, bool extend) {
  caret_ = caret;
  if (!extend) anchor_ = caret;
  open_ = false;
}

}  // namespace smalti::detail
