// Window: a top-level window and the root of its widget tree. Its children fill it; most apps
// put one Column or Row inside and build from there.
#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "smalti/widget.hpp"

namespace smalti {

class App;
namespace detail {
struct WindowState;
class Tree;
}  // namespace detail

struct WindowOptions {
  std::string title = "smalti";
  Size size{960, 640};      // client area, logical pixels
  Size min_size{320, 240};  // never more than `size`: a smaller window asked for wins
};

class Window final : public Widget {
 public:
  // Windows are created by App::open_window; the constructor is not for users.
  Window(App& app, const WindowOptions& options);
  ~Window() override;

  void set_title(std::string_view title);
  const std::string& title() const;
  // Closes the window once the current event has been handled. The app's run() returns when
  // the last window is gone.
  void close();

  float scale() const;  // device pixels per logical pixel, follows the monitor
  Size client_size() const;

  Size measure(Size available) override;
  void arrange(Rect bounds) override;
  void paint(Painter& p) override;
  bool accepts_children() const override { return true; }

  detail::WindowState& state() { return *state_; }

 private:
  friend class detail::Tree;
  std::unique_ptr<detail::WindowState> state_;
};

}  // namespace smalti
