// The sample from docs/What-it-looks-like.md, built with the examples so the page cannot drift.
#include <smalti/smalti.hpp>

#include <string>
namespace sm = smalti;

int main() {
  sm::App app;
  auto win = app.open_window({"Settings", {420, 300}});
  auto col = win->add<sm::Column>(sm::StackOptions{.spacing = 12, .padding = sm::Insets::all(24)});
  col->add<sm::Label>("Settings", sm::LabelOptions{.role = sm::TextRole::heading});

  auto card = col->add<sm::Card>();
  card->add<sm::Switch>("Live reload", true);
  auto volume = card->add<sm::Slider>(sm::SliderOptions{.min = 0, .max = 100, .value = 40});
  auto readout = card->add<sm::Label>("40", sm::LabelOptions{.role = sm::TextRole::secondary});
  volume->on_change([readout](float v) { readout->set_text(std::to_string(static_cast<int>(v))); });

  auto actions = col->add<sm::Row>(sm::StackOptions{.spacing = 8, .justify = sm::Align::end});
  actions->add<sm::Button>("Cancel")->on_click([win] { win->close(); });
  actions->add<sm::Button>("Save", sm::ButtonVariant::primary)->on_click([win] { win->close(); });
  return app.run();
}
