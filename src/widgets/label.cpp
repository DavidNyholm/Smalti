#include <algorithm>

#include "render/text.hpp"
#include "smalti/widgets.hpp"
#include "widgets/style.hpp"

namespace smalti {

Label::Label(std::string text, LabelOptions options) : text_(std::move(text)), options_(options) {}

void Label::set_text(std::string text) {
  if (text == text_) return;
  lines_.clear();  // they view the old text; the relayout requested below cuts new ones
  text_ = std::move(text);
  invalidate_layout();
}

TextStyle Label::style() const {
  const Theme& th = theme();
  switch (options_.role) {
    case TextRole::caption: return {th.text_caption, FontWeight::regular};
    case TextRole::title: return {th.text_title, FontWeight::semibold};
    case TextRole::heading: return {th.text_heading, FontWeight::semibold};
    case TextRole::body:
    case TextRole::secondary: break;
  }
  return {th.text_body, FontWeight::regular};
}

Color Label::color() const {
  const Theme& th = theme();
  switch (options_.role) {
    case TextRole::secondary: return th.text_secondary;
    case TextRole::caption: return th.text_muted;
    default: return th.text;
  }
}

Size Label::measure(Size available) {
  const TextStyle st = style();
  const float line = detail::text_line_height(st);
  if (!options_.wrap) return {detail::measure_text(text_, st), line};
  const auto lines = detail::wrap_lines(detail::app_state().fonts.get(st.weight), st.size, text_, available.width);
  float width = 0;
  for (std::string_view l : lines) width = std::max(width, detail::measure_text(l, st));
  return {width, line * static_cast<float>(lines.size())};
}

void Label::arrange(Rect bounds) {
  Widget::arrange(bounds);
  if (options_.wrap) {
    const TextStyle st = style();
    lines_ = detail::wrap_lines(detail::app_state().fonts.get(st.weight), st.size, text_, bounds.width);
  }
}

void Label::paint(Painter& p) {
  const TextStyle st = style();
  const Rect b = bounds();
  const Color c = detail::dim_if(!enabled(), color());
  if (!options_.wrap) {
    p.text({b.x, detail::centered_baseline(b, st)}, text_, st, c);
    return;
  }
  const float line = detail::text_line_height(st);
  float baseline = b.y + (line - detail::text_ascent(st) - detail::text_descent(st)) * 0.5f + detail::text_ascent(st);
  for (std::string_view l : lines_) {
    p.text({b.x, baseline}, l, st, c);
    baseline += line;
  }
}

}  // namespace smalti
