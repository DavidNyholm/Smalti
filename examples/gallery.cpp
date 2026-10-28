// The gallery: every v0.1 control in one window. The interface itself is in gallery_ui.hpp,
// shared with the screenshot renderer.
#include "gallery_ui.hpp"

int main() {
  sm::App app;
  auto win = app.open_window({"smalti gallery", {1100, 720}});
  gallery::build(app, win);
  return app.run();
}
