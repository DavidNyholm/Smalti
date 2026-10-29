// Reading a theme file's "widgets" section into style rules, checked against the widgets'
// schema. Values that name tokens are resolved later, once the tokens are.
#pragma once

#include <vector>

#include "theme/jsonc.hpp"
#include "theme/tokens.hpp"
#include "theme/widget_styles.hpp"

namespace smalti::detail {

// A rule as the file writes it: the value is a token expression until the tokens are known.
struct PendingStyleRule {
  StyleRule rule;  // its value fields are filled in when resolved
  StyleProperty::Kind kind = StyleProperty::Kind::color;
  TokenExpr expr;     // colours and radii
  float opacity = 1;  // "opacity"
};

void read_widgets_section(const JsonValue& widgets, std::vector<PendingStyleRule>& out,
                          ThemeErrors& errors);

}  // namespace smalti::detail
