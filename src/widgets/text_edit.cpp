// The editing model of a one-line text field (see text_edit.hpp).
#include "widgets/text_edit.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <utility>

#include "platform/text_input.hpp"
#include "render/text.hpp"

namespace smalti::detail {

namespace {

constexpr size_t kMaxUndoSteps = 1000;  // older steps are forgotten
constexpr size_t kFirstStepRoom = 16;   // history grows by doubling from this

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
  // Each word typed after a space is an undo step of its own, however the text arrives: a key
  // at a time, or a whole phrase from an input method at once.
  std::vector<size_t> word_starts;
  CharKind before = CharKind::word;
  for (size_t i = 0; i < added.size();) {
    const size_t at = i;
    const CharKind kind = kind_of(next_codepoint(added, i));
    if (at > 0 && kind != CharKind::space && before == CharKind::space) word_starts.push_back(at);
    before = kind;
  }
  replace(selection_start(), selection_end() - selection_start(), added, StepKind::typing,
          word_starts);
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
  // What may run out of memory comes before anything changes (see replace).
  text_.reserve(text_.size() - undo_.back().added.size() + undo_.back().removed.size());
  room_for(redo_, 1);
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
  text_.reserve(text_.size() - redo_.back().removed.size() + redo_.back().added.size());
  room_for(undo_, 1);
  Step s = std::move(redo_.back());
  redo_.pop_back();
  text_.replace(s.at, s.removed.size(), s.added);
  caret_ = s.caret_after;
  anchor_ = s.anchor_after;
  undo_.push_back(std::move(s));
  open_ = false;
  return true;
}

void TextEdit::room_for(std::vector<Step>& steps, size_t more) {
  if (steps.size() + more <= steps.capacity()) return;
  steps.reserve(std::max({kFirstStepRoom, steps.capacity() * 2, steps.size() + more}));
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

void TextEdit::replace(size_t at, size_t length, std::string_view added, StepKind kind,
                       std::span<const size_t> word_starts) {
  // A step for each piece of `added` from one word start to the next; the first replaces the
  // selection, and may join the last step. Every one is made before anything changes.
  std::vector<Step> steps;
  steps.reserve(word_starts.size() + 1);
  for (size_t i = 0, from = 0; i <= word_starts.size(); ++i) {
    const size_t to = i < word_starts.size() ? word_starts[i] : added.size();
    Step step;
    step.at = at + from;
    if (i == 0) step.removed = text_.substr(at, length);
    step.added = added.substr(from, to - from);
    step.caret_before = i == 0 ? caret_ : at + from;
    step.anchor_before = i == 0 ? anchor_ : at + from;
    step.caret_after = step.anchor_after = at + to;
    step.kind = kind;
    steps.push_back(std::move(step));
    from = to;
  }
  // Everything that may run out of memory comes before anything changes, so a failure leaves
  // the text and its history as they were: the steps hold byte offsets into this very text.
  // (Only the first piece can join the last step: the others start a word after a space.)
  text_.reserve(text_.size() - length + added.size());
  const bool merging = merges(steps.front());
  std::string merged = merging ? merged_text(steps.front()) : std::string();
  const size_t fresh = steps.size() - (merging ? 1 : 0);
  const size_t kept = std::min(kMaxUndoSteps, undo_.size() + fresh);
  room_for(undo_, kept > undo_.size() ? kept - undo_.size() : 0);
  text_.replace(at, length, added);
  caret_ = anchor_ = at + added.size();
  redo_.clear();
  if (merging) absorb(steps.front(), std::move(merged));
  // Past the limit the oldest steps are forgotten, those just made too if they alone exceed it.
  auto first_new = steps.begin() + (merging ? 1 : 0);
  if (undo_.size() + fresh > kMaxUndoSteps) {
    const size_t drop = undo_.size() + fresh - kMaxUndoSteps;
    if (drop <= undo_.size()) {
      undo_.erase(undo_.begin(), undo_.begin() + static_cast<std::ptrdiff_t>(drop));
    } else {
      first_new += static_cast<std::ptrdiff_t>(drop - undo_.size());
      undo_.clear();
    }
  }
  undo_.insert(undo_.end(), std::make_move_iterator(first_new),
               std::make_move_iterator(steps.end()));
  open_ = kind != StepKind::other;
}

// Typing at the end of the last typing step, or erasing next to the last erasing step, adds to
// that step while it is open.
bool TextEdit::merges(const Step& step) const {
  if (!open_ || undo_.empty() || undo_.back().kind != step.kind) return false;
  const Step& last = undo_.back();
  switch (step.kind) {
    case StepKind::typing: {
      if (!step.removed.empty() || step.at != last.at + last.added.size()) return false;
      // A word typed after a space starts a step of its own. (The step adds to the last one,
      // so something stands before it.)
      size_t first = 0, before = prev(step.at);
      return kind_of(next_codepoint(step.added, first)) == CharKind::space ||
             kind_of(next_codepoint(text_, before)) != CharKind::space;
    }
    case StepKind::erasing_back:
      return step.at + step.removed.size() == last.at;
    case StepKind::erasing_forward:
      return step.at == last.at;
    case StepKind::other:
      break;
  }
  return false;
}

// What the last step holds once `step` joins it: all the text typed, or all the text erased.
std::string TextEdit::merged_text(const Step& step) const {
  const Step& last = undo_.back();
  switch (step.kind) {
    case StepKind::typing:
      return last.added + step.added;
    case StepKind::erasing_back:
      return step.removed + last.removed;
    case StepKind::erasing_forward:
      return last.removed + step.removed;
    case StepKind::other:
      break;
  }
  return {};
}

void TextEdit::absorb(const Step& step, std::string merged) {
  Step& last = undo_.back();
  if (step.kind == StepKind::typing) {
    last.added = std::move(merged);
  } else {
    last.removed = std::move(merged);
  }
  if (step.kind == StepKind::erasing_back) last.at = step.at;
  last.caret_after = step.caret_after;
  last.anchor_after = step.anchor_after;
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
