// TextBox: the editing model (text_edit.hpp) with a caret, a selection, scrolling, the
// clipboard, and a caret that blinks on timed redraws, so an idle focused box costs two frames
// a second while it blinks and none once it stops.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>

#include "render/text.hpp"
#include "smalti/app.hpp"
#include "smalti/widgets.hpp"
#include "widgets/style.hpp"
#include "widgets/text_edit.hpp"

namespace smalti {

namespace {

using Unit = detail::TextEdit::Unit;

constexpr float kDefaultWidth = 240;  // what it asks for; layouts usually stretch it
constexpr float kTextInset = 10;      // between the border and the text
// The text shows this far into the inset, so a caret at either end of a scrolled line shows.
constexpr float kCaretRoom = 2;
// The caret blinks this long after the last input, then stays (decision D17).
constexpr int64_t kBlinkForNs = 10'000'000'000;
// Typing after a pause this long starts a new undo step.
constexpr int64_t kTypingPauseNs = 1'000'000'000;
// A drag past either end scrolls this many logical pixels a second for each pixel past it.
constexpr float kEdgeScrollPerPixel = 12;
constexpr std::string_view kBullet =
    "\xE2\x80\xA2";  // U+2022, shown for each character of a password

bool continuation(char c) { return (static_cast<uint8_t>(c) & 0xC0) == 0x80; }

}  // namespace

TextBox::TextBox(TextBoxOptions options)
    : edit_(std::make_unique<detail::TextEdit>(options.max_length)),
      placeholder_(std::move(options.placeholder)),
      password_(options.password) {
  edit_->set_text(options.text);
}

TextBox::TextBox(std::string placeholder)
    : TextBox([&] {
        TextBoxOptions o;
        o.placeholder = std::move(placeholder);
        return o;
      }()) {}

TextBox::~TextBox() = default;

TextBox& TextBox::on_change(Callback<void(const std::string&)> fn) {
  on_change_ = std::move(fn);
  return *this;
}

TextBox& TextBox::on_submit(Callback<void(const std::string&)> fn) {
  on_submit_ = std::move(fn);
  return *this;
}

void TextBox::set_text(std::string_view text) {
  edit_->set_text(text);
  stops_size_ = 0;
  caret_moved();
}

const std::string& TextBox::text() const { return edit_->text(); }

void TextBox::set_placeholder(std::string placeholder) {
  placeholder_ = std::move(placeholder);
  invalidate();
}

void TextBox::set_password(bool password) {
  if (password == password_) return;
  password_ = password;
  stops_size_ = 0;
  caret_moved();
}

void TextBox::set_max_length(size_t characters) {
  edit_->set_max_chars(characters);
  stops_size_ = 0;
  caret_moved();
}

void TextBox::select_all() {
  edit_->select_all();
  caret_moved();
}

TextStyle TextBox::text_style() const { return detail::body_text(theme()); }

Size TextBox::measure(Size) {
  const Theme& th = theme();
  return {kDefaultWidth, std::max(th.control_height, detail::text_line_height(text_style()))};
}

void TextBox::arrange(Rect bounds) {
  Widget::arrange(bounds);
  keep_caret_visible();
}

Rect TextBox::text_area() const {
  const Rect b = bounds();
  return {b.x + kTextInset, b.y, std::max(0.0f, b.width - 2 * kTextInset), b.height};
}

float TextBox::text_origin() const { return text_area().x - scroll_; }

// The stops are measured on the text as shown, a dot per character for a password, and point
// at the text's own bytes.
void TextBox::refresh_stops() const {
  const TextStyle st = text_style();
  if (stops_size_ == st.size) return;
  const std::string& text = edit_->text();
  if (password_) {
    shown_.clear();
    for (const char c : text) {
      if (!continuation(c)) shown_ += kBullet;
    }
  } else {
    shown_ = text;
  }
  const auto stops = detail::caret_stops(detail::app_state().fonts.get(st.weight), st.size, shown_);
  stop_x_.clear();
  stop_byte_.clear();
  size_t byte = 0;
  for (const detail::CaretStop& s : stops) {
    stop_x_.push_back(s.x);
    stop_byte_.push_back(password_ ? byte : s.byte);
    // The next character of the text itself: a password's dots are all one size.
    if (byte < text.size()) {
      do ++byte;
      while (byte < text.size() && continuation(text[byte]));
    }
  }
  stops_size_ = st.size;
}

size_t TextBox::stop_of(size_t byte) const {
  refresh_stops();
  const auto it = std::lower_bound(stop_byte_.begin(), stop_byte_.end(), byte);
  return static_cast<size_t>(std::min(it, stop_byte_.end() - 1) - stop_byte_.begin());
}

// The caret stop nearest to window x.
size_t TextBox::byte_at(float x) const {
  refresh_stops();
  const float local = x - text_origin();
  auto it = std::lower_bound(stop_x_.begin(), stop_x_.end(), local);
  if (it == stop_x_.end()) return stop_byte_.back();
  if (it != stop_x_.begin() && local - *(it - 1) < *it - local) --it;
  return stop_byte_[static_cast<size_t>(it - stop_x_.begin())];
}

void TextBox::keep_caret_visible() {
  refresh_stops();
  const float width = text_area().width;
  const float total = stop_x_.back();
  if (width <= 0 || total <= width) {
    scroll_ = 0;
    return;
  }
  const float caret = stop_x_[stop_of(edit_->caret())];
  if (caret - scroll_ > width) scroll_ = caret - width;
  if (caret - scroll_ < 0) scroll_ = caret;
  scroll_ = std::clamp(scroll_, 0.0f, total - width);  // no empty space after a scrolled line
}

void TextBox::caret_moved() {
  keep_caret_visible();
  blink_start_ns_ = now_ns();  // the caret shows at once after any input
  text_caret_moved();
  invalidate();
}

void TextBox::edited() {
  stops_size_ = 0;
  last_edit_ns_ = now_ns();
  caret_moved();
  if (on_change_) on_change_(edit_->text());
}

void TextBox::end_step_after_pause() {
  if (now_ns() - last_edit_ns_ >= kTypingPauseNs) edit_->break_step();
}

Rect TextBox::text_caret() const {
  const TextStyle st = text_style();
  const float ascent = detail::text_ascent(st);
  const float top = detail::centered_baseline(bounds(), st) - ascent;
  // Where it really is: scrolling keeps it inside the box.
  const float x = text_origin() + stop_x_[stop_of(edit_->caret())];
  return {x, top, 1, ascent + detail::text_descent(st)};
}

bool TextBox::caret_shown() {
  const int blink_ms = detail::app_state().system.caret_blink_ms;
  if (blink_ms <= 0) return true;
  const int64_t period = int64_t{blink_ms} * 1'000'000;
  const int64_t elapsed = std::max<int64_t>(0, frame_time_ns() - blink_start_ns_);
  if (elapsed >= kBlinkForNs) return true;
  const int64_t phase = elapsed / period;
  const bool shown = phase % 2 == 0;
  const int64_t next = blink_start_ns_ + (phase + 1) * period;
  const int64_t stop = blink_start_ns_ + kBlinkForNs;
  // After the last blink the caret stays shown: a hidden one comes back then, a shown one
  // needs no frame.
  if (next < stop) {
    redraw_at(next);
  } else if (!shown) {
    redraw_at(stop);
  }
  return shown;
}

void TextBox::edge_scroll() {
  const Rect area = text_area();
  const float past = drag_x_ < area.x                ? drag_x_ - area.x
                     : drag_x_ > area.x + area.width ? drag_x_ - (area.x + area.width)
                                                     : 0.0f;
  if (!selecting_ || past == 0) {
    edge_scroll_ns_ = 0;
    return;
  }
  const int64_t now = frame_time_ns();
  if (edge_scroll_ns_ != 0) {
    const float seconds = static_cast<float>(now - edge_scroll_ns_) * 1e-9f;
    refresh_stops();
    const float most = std::max(0.0f, stop_x_.back() - area.width);
    scroll_ = std::clamp(scroll_ + past * kEdgeScrollPerPixel * seconds, 0.0f, most);
    edit_->move_to(byte_at(std::clamp(drag_x_, area.x, area.x + area.width)), true);
    text_caret_moved();
  }
  edge_scroll_ns_ = now;
  keep_animating();
}

void TextBox::paint(Painter& p) {
  using S = detail::TextBoxSlot;
  const Rect b = bounds();
  const detail::WidgetStyle& s = detail::widget_style(detail::StyledWidget::text_box);
  const detail::StyleMix mix{.hover = at(hover_), .focused = has_focus(), .disabled = !enabled()};
  const Color fill = s.color(S::fill, mix), border = s.color(S::border, mix);
  if (fill.a > 0) p.fill_rounded_rect(b, s.radius, fill);
  if (border.a > 0) p.stroke_rounded_rect(b, s.radius, 0, border);

  edge_scroll();
  refresh_stops();
  const TextStyle st = text_style();
  const float baseline = detail::centered_baseline(b, st);
  const float ascent = detail::text_ascent(st);
  const float height = ascent + detail::text_descent(st);
  const Rect area = text_area();
  p.push_clip({area.x - kCaretRoom, b.y, area.width + 2 * kCaretRoom, b.height});
  const float origin = text_origin();
  if (edit_->text().empty()) {
    if (!placeholder_.empty())
      p.text({area.x, baseline}, placeholder_, st, s.color(S::placeholder, mix));
  } else {
    if (has_focus() && edit_->has_selection()) {
      const float from = origin + stop_x_[stop_of(edit_->selection_start())];
      const float to = origin + stop_x_[stop_of(edit_->selection_end())];
      p.fill_rect({from, baseline - ascent, to - from, height}, s.color(S::selection, mix));
    }
    p.text({origin, baseline}, shown_, st, s.color(S::text, mix));
  }
  if (has_focus() && caret_shown()) {
    // One device pixel wide at any scale, on a whole device pixel.
    const float x = std::round((origin + stop_x_[stop_of(edit_->caret())]) * p.scale()) / p.scale();
    p.fill_rect({x, baseline - ascent, p.hairline(), height}, s.color(S::caret, mix));
  }
  p.pop_clip();
}

bool TextBox::on_mouse(const MouseEvent& e) {
  switch (e.kind) {
    case MouseEvent::Kind::press: {
      if (e.button != MouseButton::left) return false;
      const size_t byte = byte_at(e.position.x);
      if (e.clicks >= 3) {
        edit_->select_all();
      } else if (e.clicks == 2) {
        edit_->select_word(byte);
      } else {
        edit_->move_to(byte, e.modifiers.shift);
      }
      selecting_ = true;
      drag_x_ = e.position.x;
      edge_scroll_ns_ = 0;
      caret_moved();
      return true;
    }
    case MouseEvent::Kind::move:
      if (!selecting_) return false;
      drag_x_ = e.position.x;
      edit_->move_to(byte_at(e.position.x), true);
      caret_moved();
      return true;
    case MouseEvent::Kind::release:
    case MouseEvent::Kind::cancel:
      if (!selecting_) return false;
      selecting_ = false;
      invalidate();
      return true;
    default:
      return false;
  }
}

bool TextBox::on_key(const KeyEvent& e) {
  if (!e.down) return false;
  const bool shift = e.modifiers.shift;
  // Ctrl with Alt is AltGr on many keyboards, which types characters: not a shortcut.
  const bool ctrl = e.modifiers.ctrl && !e.modifiers.alt;
  const Unit unit = ctrl ? Unit::word : Unit::character;
  switch (e.key) {
    case Key::left:
      edit_->move_left(unit, shift);
      caret_moved();
      return true;
    case Key::right:
      edit_->move_right(unit, shift);
      caret_moved();
      return true;
    case Key::home:
      edit_->move_home(shift);
      caret_moved();
      return true;
    case Key::end:
      edit_->move_end(shift);
      caret_moved();
      return true;
    case Key::backspace:
      end_step_after_pause();
      if (edit_->erase_back(unit)) edited();
      return true;
    case Key::delete_:
      if (shift && !ctrl) {
        cut();
        return true;
      }
      end_step_after_pause();
      if (edit_->erase_forward(unit)) edited();
      return true;
    case Key::insert:
      if (ctrl && !shift) {
        copy();
      } else if (shift && !ctrl) {
        paste();
      } else {
        return false;
      }
      return true;
    case Key::enter:
      if (!on_submit_) return false;
      on_submit_(edit_->text());
      return true;
    default:
      break;
  }
  if (!ctrl) return false;
  switch (e.key) {
    case Key::a:
      select_all();
      return true;
    case Key::c:
      copy();
      return true;
    case Key::x:
      cut();
      return true;
    case Key::v:
      paste();
      return true;
    case Key::z:
      if (shift ? edit_->redo() : edit_->undo()) edited();
      return true;
    case Key::y:
      if (edit_->redo()) edited();
      return true;
    default:
      return false;
  }
}

bool TextBox::on_text(std::string_view utf8) {
  end_step_after_pause();
  if (edit_->type(utf8)) edited();
  return true;
}

void TextBox::copy() {
  // A password's characters stay in the box.
  if (password_ || !edit_->has_selection()) return;
  App::current().set_clipboard_text(edit_->selected());
}

void TextBox::cut() {
  if (password_ || !edit_->has_selection()) return;
  if (App::current().set_clipboard_text(edit_->selected()) && edit_->erase_selection()) edited();
}

void TextBox::paste() {
  const std::optional<std::string> text = App::current().clipboard_text();
  if (text && edit_->insert(*text)) edited();
}

void TextBox::on_focus_changed(bool gained) {
  if (!gained) {
    edit_->break_step();
    selecting_ = false;
  }
  blink_start_ns_ = now_ns();
  invalidate();
}

void TextBox::on_hover_changed(bool hovered) {
  animate(hover_, hovered ? 1.0f : 0.0f, theme().motion_fast);
}

}  // namespace smalti
