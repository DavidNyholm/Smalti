// The editing model of a one-line text field, apart from any widget: UTF-8 text, a caret and an
// anchor at character boundaries, edits that replace the selection, movement by character and
// by word, and undo with typing grouped into steps. Pure logic, tested on its own.
//
// Characters are code points (decision D18): an emoji with a skin tone, or a letter followed by
// a separate combining accent, takes more than one step. Keyboards in Latin, Greek and Cyrillic
// scripts type precomposed letters, which are one step.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace smalti::detail {

// What a one-line field takes from text: valid UTF-8 (each invalid byte becomes U+FFFD), line
// breaks and tabs inside as spaces, those at either end dropped, and no other controls.
std::string one_line(std::string_view utf8);

class TextEdit {
 public:
  enum class Unit : uint8_t { character, word };

  // max_chars: the most characters the text may hold; 0 for no limit.
  explicit TextEdit(size_t max_chars = 0) : max_chars_(max_chars) {}

  const std::string& text() const { return text_; }
  size_t caret() const { return caret_; }    // a byte offset
  size_t anchor() const { return anchor_; }  // where the selection started; the caret if none
  bool has_selection() const { return caret_ != anchor_; }
  size_t selection_start() const { return caret_ < anchor_ ? caret_ : anchor_; }
  size_t selection_end() const { return caret_ < anchor_ ? anchor_ : caret_; }
  std::string_view selected() const;

  // Replaces everything, made one line and cut to the limit; the caret goes to the end and
  // the undo history is cleared.
  void set_text(std::string_view utf8);
  void set_max_chars(size_t max_chars);  // cuts the text if it holds more

  // Edits replace the selection. Each returns whether the text changed.
  // Typed text: a run of typing is one undo step, until the caret moves, break_step(), or a
  // word starts after a space.
  bool type(std::string_view utf8);
  bool insert(std::string_view utf8);  // pasted text: a step of its own
  bool erase_back(Unit unit);          // Backspace: the selection, or what is before the caret
  bool erase_forward(Unit unit);       // Delete: the selection, or what is after it
  bool erase_selection();              // cut

  // Moves the caret; `extend` keeps the anchor where it is (Shift held).
  void move_left(Unit unit, bool extend);
  void move_right(Unit unit, bool extend);
  void move_home(bool extend);
  void move_end(bool extend);
  void move_to(size_t byte, bool extend);  // a byte inside a character means its start
  void select_all();
  // The run of one kind of character (letters and digits, punctuation, or spaces) at `byte`.
  void select_word(size_t byte);

  bool undo();
  bool redo();
  // Ends the step that typing or erasing is adding to: the widget calls it after a pause.
  void break_step() { open_ = false; }

  // Where a word move from `byte` lands, as Windows editors do: left to the start of the word
  // before (spaces skipped first), right past the word and the spaces after it.
  size_t word_left(size_t byte) const;
  size_t word_right(size_t byte) const;

 private:
  enum class StepKind : uint8_t { typing, erasing_back, erasing_forward, other };
  struct Step {
    size_t at = 0;
    std::string removed, added;
    size_t caret_before = 0, anchor_before = 0, caret_after = 0, anchor_after = 0;
    StepKind kind = StepKind::other;
  };

  // Replaces `length` bytes at `at` with `added` (already one line), records the step, and puts
  // the caret after the new text.
  void replace(size_t at, size_t length, std::string_view added, StepKind kind);
  bool merge(const Step& step);
  std::string fit(std::string_view added) const;  // cut to what the limit leaves room for
  size_t next(size_t byte) const;
  size_t prev(size_t byte) const;
  void place(size_t caret, bool extend);

  std::string text_;
  size_t caret_ = 0, anchor_ = 0;
  size_t max_chars_;
  std::vector<Step> undo_, redo_;
  bool open_ = false;  // the last undo step may still grow
};

}  // namespace smalti::detail
