// The smallest smalti program: a window, a heading, a label and a button that counts.
#include <string>

#include <smalti/smalti.hpp>

namespace sm = smalti;

int main() {
  sm::App app;
  auto win = app.open_window({"Hello, smalti", {380, 220}});
  auto col = win->add<sm::Column>(sm::StackOptions{.spacing = 12, .padding = sm::Insets::all(28)});
  col->add<sm::Label>("Hello, smalti", sm::LabelOptions{.role = sm::TextRole::heading});
  auto status = col->add<sm::Label>("Nothing clicked yet.",
                                    sm::LabelOptions{.role = sm::TextRole::secondary});
  col->add<sm::Spacer>();
  auto button = col->add<sm::Button>("Click me", sm::ButtonVariant::primary);
  button->set_align(sm::Align::start);
  // The Ref is captured by value: if the label were removed, using it would be a reported
  // error, never a read of freed memory.
  button->on_click([status, clicks = 0]() mutable {
    ++clicks;
    status->set_text("Clicked " + std::to_string(clicks) + (clicks == 1 ? " time." : " times."));
  });
  return app.run();
}
