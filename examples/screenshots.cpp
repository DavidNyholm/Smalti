// Renders the gallery in each built-in theme without a screen, as binary PPM images, for the
// documentation. tools/screenshots.py runs this and converts the images to PNG.
//
//   screenshots <output directory>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>

#include <smalti/testing.hpp>

#include "gallery_ui.hpp"

namespace {

using namespace std::chrono_literals;

bool write_ppm(const std::string& path, const sm::testing::Driver& d) {
  std::ofstream out(path, std::ios::binary);
  if (!out) return false;
  out << "P6\n" << d.width() << ' ' << d.height() << "\n255\n";
  for (int y = 0; y < d.height(); ++y) {
    for (int x = 0; x < d.width(); ++x) {
      const uint32_t px = d.pixel(x, y);
      const char rgb[3] = {static_cast<char>(px >> 16), static_cast<char>(px >> 8),
                           static_cast<char>(px)};
      out.write(rgb, 3);
    }
  }
  return static_cast<bool>(out);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: screenshots <output directory>\n");
    return 2;
  }
  const std::string dir = argv[1];
  for (const sm::Theme* theme : {&sm::theme_dark(), &sm::theme_black(), &sm::theme_light()}) {
    sm::App app(sm::AppOptions{.backend = sm::Backend::headless, .theme = theme});
    auto win = app.open_window({"smalti gallery", {1100, 720}});
    gallery::build(app, win);
    sm::testing::Driver d(win);
    d.advance(600ms);  // let entrance state settle
    std::string name = theme->name;
    for (char& c : name) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    const std::string path = dir + "/gallery-" + name + ".ppm";
    if (!write_ppm(path, d)) {
      std::fprintf(stderr, "screenshots: cannot write %s\n", path.c_str());
      return 1;
    }
    std::printf("wrote %s\n", path.c_str());
  }
  return 0;
}
