// The X11 backend, through Xlib. Wayland desktops run it through XWayland. v0.1 presents with
// XPutImage (the CPU path); frames are paced by a timer at the display's refresh rate.
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <stdexcept>
#include <string>
#include <vector>

#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <sys/eventfd.h>

#include "platform/platform.hpp"

namespace smalti::platform {

namespace {

constexpr double kRefreshHz = 60.0;  // XRandR is not consulted yet; 60 Hz is the safe default
constexpr float kDefaultDpi = 96.0f;
constexpr float kWheelNotchPixels = 120.0f;  // one wheel notch, in logical pixels
constexpr unsigned long kDoubleClickMs = 400;
constexpr int kDoubleClickSlop = 4;  // pixels the pointer may move between the two clicks
constexpr int kBitsPerPixel = 32;
constexpr long kEventMask = ExposureMask | KeyPressMask | KeyReleaseMask | ButtonPressMask |
                            ButtonReleaseMask | PointerMotionMask | EnterWindowMask |
                            LeaveWindowMask | StructureNotifyMask | FocusChangeMask;

int64_t monotonic_ns() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<int64_t>(ts.tv_sec) * 1'000'000'000 + ts.tv_nsec;
}

// Xft.dpi from the X resource database, as desktop environments set it for scaling.
float display_scale(Display* d) {
  const char* resources = XResourceManagerString(d);
  if (!resources) return 1.0f;
  XrmInitialize();
  const XrmDatabase db = XrmGetStringDatabase(resources);
  if (!db) return 1.0f;
  char* type = nullptr;
  XrmValue value{};
  float scale = 1.0f;
  if (XrmGetResource(db, "Xft.dpi", "Xft.Dpi", &type, &value) && value.addr) {
    const float dpi = std::strtof(value.addr, nullptr);
    if (dpi > 0) scale = dpi / kDefaultDpi;
  }
  XrmDestroyDatabase(db);
  return scale;
}

Modifiers modifiers_of(unsigned int state) {
  return {(state & ShiftMask) != 0, (state & ControlMask) != 0, (state & Mod1Mask) != 0,
          (state & Mod4Mask) != 0};
}

Key map_keysym(KeySym k) {
  if (k >= XK_a && k <= XK_z)
    return static_cast<Key>(static_cast<int>(Key::a) + static_cast<int>(k - XK_a));
  if (k >= XK_A && k <= XK_Z)
    return static_cast<Key>(static_cast<int>(Key::a) + static_cast<int>(k - XK_A));
  if (k >= XK_0 && k <= XK_9)
    return static_cast<Key>(static_cast<int>(Key::digit0) + static_cast<int>(k - XK_0));
  if (k >= XK_F1 && k <= XK_F12)
    return static_cast<Key>(static_cast<int>(Key::f1) + static_cast<int>(k - XK_F1));
  switch (k) {
    case XK_space:
      return Key::space;
    case XK_Return:
    case XK_KP_Enter:
      return Key::enter;
    case XK_Escape:
      return Key::escape;
    case XK_Tab:
    case XK_ISO_Left_Tab:
      return Key::tab;
    case XK_BackSpace:
      return Key::backspace;
    case XK_Delete:
      return Key::delete_;
    case XK_Left:
      return Key::left;
    case XK_Right:
      return Key::right;
    case XK_Up:
      return Key::up;
    case XK_Down:
      return Key::down;
    case XK_Home:
      return Key::home;
    case XK_End:
      return Key::end;
    case XK_Prior:
      return Key::page_up;
    case XK_Next:
      return Key::page_down;
    default:
      return Key::unknown;
  }
}

unsigned int cursor_shape(Cursor c) {
  switch (c) {
    case Cursor::hand:
      return XC_hand2;
    case Cursor::text:
      return XC_xterm;
    case Cursor::resize_horizontal:
      return XC_sb_h_double_arrow;
    case Cursor::resize_vertical:
      return XC_sb_v_double_arrow;
    case Cursor::arrow:
      break;
  }
  return XC_left_ptr;
}

}  // namespace

class X11Backend;

class X11Window final : public NativeWindow {
 public:
  X11Window(X11Backend& backend, const WindowOptions& options, WindowEvents& events);
  ~X11Window() override;

  void show() override;
  void set_title(std::string_view utf8) override;
  PixelBuffer frame_buffer() override { return {pixels_.data(), width_, height_, width_}; }
  void present(std::span<const RectI> dirty) override;
  void request_frame() override { requested_ = true; }
  bool frame_requested() const override { return requested_; }
  void set_cursor(Cursor c) override;
  void capture_mouse(bool) override {}  // the X server grabs the pointer while a button is down
  void set_dark_frame(bool dark) override;
  float scale() const override { return scale_; }

  void handle(XEvent& e);
  ::Window id() const { return window_; }
  int64_t next_frame_due() const { return requested_ ? last_frame_ + kFrameNs : INT64_MAX; }
  void run_frame_if_due(int64_t now);

 private:
  static constexpr int64_t kFrameNs = static_cast<int64_t>(1e9 / kRefreshHz);
  void resize(int w, int h);
  void blit(int x, int y, int w, int h);
  void release();  // frees whatever exists so far; the destructor, or a failed constructor

  X11Backend& backend_;
  WindowEvents& events_;
  Display* display_;
  ::Window window_ = 0;
  GC gc_ = nullptr;
  XImage* image_ = nullptr;
  std::vector<uint32_t> pixels_;  // owned here; the XImage only borrows it
  int width_ = 0, height_ = 0;
  float scale_ = 1;
  bool requested_ = false;
  int64_t last_frame_ = 0;
  Time last_press_time_ = 0;
  int last_press_x_ = 0, last_press_y_ = 0;
  std::vector<unsigned int> keys_down_;  // to tell auto-repeat from a fresh press
};

class X11Backend final : public PlatformBackend {
 public:
  X11Backend(Display* display, BackendHost& host);
  ~X11Backend() override;

  std::unique_ptr<NativeWindow> create_window(const WindowOptions& options,
                                              WindowEvents& events) override;
  void run() override;
  void quit() override { quit_ = true; }
  void wake() override;
  int64_t now_ns() const override { return monotonic_ns(); }
  SystemInfo system_info() const override;

  Display* display() const { return display_; }
  Atom atom(const char* name) { return XInternAtom(display_, name, False); }
  ::Cursor cursor(Cursor c);
  void add(X11Window* w) { windows_.push_back(w); }
  void forget(X11Window* w) { std::erase(windows_, w); }
  float scale() const { return scale_; }

 private:
  void pump_events();

  Display* display_;
  BackendHost& host_;
  int wake_fd_ = -1;
  float scale_ = 1;
  std::vector<X11Window*> windows_;
  std::vector<std::pair<unsigned int, ::Cursor>> cursors_;
  bool quit_ = false;
};

// --- X11Window ------------------------------------------------------------------------------

X11Window::X11Window(X11Backend& backend, const WindowOptions& options, WindowEvents& events)
    : backend_(backend), events_(events), display_(backend.display()), scale_(backend.scale()) {
  const int screen = DefaultScreen(display_);
  const int w = std::max(1, static_cast<int>(std::lround(options.size.width * scale_)));
  const int h = std::max(1, static_cast<int>(std::lround(options.size.height * scale_)));
  XSetWindowAttributes attrs{};
  attrs.background_pixmap = None;  // no flash of the server's background: we draw everything
  attrs.event_mask = kEventMask;
  window_ = XCreateWindow(display_, RootWindow(display_, screen), 0, 0, static_cast<unsigned>(w),
                          static_cast<unsigned>(h), 0, CopyFromParent, InputOutput, CopyFromParent,
                          CWBackPixmap | CWEventMask, &attrs);
  if (!window_) throw std::runtime_error("smalti: XCreateWindow failed");
  try {
    Atom delete_atom = backend_.atom("WM_DELETE_WINDOW");
    XSetWMProtocols(display_, window_, &delete_atom, 1);
    if (XSizeHints* hints = XAllocSizeHints()) {
      hints->flags = PMinSize;
      hints->min_width = static_cast<int>(options.min_size.width * scale_);
      hints->min_height = static_cast<int>(options.min_size.height * scale_);
      XSetWMNormalHints(display_, window_, hints);
      XFree(hints);
    }
    gc_ = XCreateGC(display_, window_, 0, nullptr);
    set_title(options.title);
    resize(w, h);
    backend_.add(this);  // last: the backend never sees a half-made window
  } catch (...) {
    release();
    throw;
  }
}

X11Window::~X11Window() {
  backend_.forget(this);
  release();
}

void X11Window::release() {
  if (image_) {
    image_->data = nullptr;  // the pixels belong to pixels_, not to Xlib
    XDestroyImage(image_);
    image_ = nullptr;
  }
  if (gc_) XFreeGC(display_, gc_);
  gc_ = nullptr;
  XDestroyWindow(display_, window_);
  XFlush(display_);
}

void X11Window::show() {
  XMapWindow(display_, window_);
  XFlush(display_);
}

void X11Window::set_title(std::string_view utf8) {
  const std::string title(utf8);
  XStoreName(display_, window_, title.c_str());
  XChangeProperty(display_, window_, backend_.atom("_NET_WM_NAME"), backend_.atom("UTF8_STRING"), 8,
                  PropModeReplace, reinterpret_cast<const unsigned char*>(title.data()),
                  static_cast<int>(title.size()));
}

void X11Window::set_dark_frame(bool dark) {
  // Read by GNOME's window manager and some others; ignored harmlessly elsewhere.
  const char* variant = dark ? "dark" : "light";
  XChangeProperty(display_, window_, backend_.atom("_GTK_THEME_VARIANT"),
                  backend_.atom("UTF8_STRING"), 8, PropModeReplace,
                  reinterpret_cast<const unsigned char*>(variant),
                  static_cast<int>(std::strlen(variant)));
}

void X11Window::set_cursor(Cursor c) { XDefineCursor(display_, window_, backend_.cursor(c)); }

void X11Window::resize(int w, int h) {
  const int new_w = std::max(1, w), new_h = std::max(1, h);
  // The new pixels and image are made before the old ones go: if either fails, the pixels the
  // core is drawing into stay valid.
  std::vector<uint32_t> pixels(static_cast<size_t>(new_w) * static_cast<size_t>(new_h),
                               0xFF000000u);
  const int screen = DefaultScreen(display_);
  // 32 bits per pixel, depth 24: on a little-endian machine with the usual masks this is
  // exactly the core's 0xAARRGGBB layout, so no conversion is needed.
  XImage* image = XCreateImage(display_, DefaultVisual(display_, screen),
                               static_cast<unsigned>(DefaultDepth(display_, screen)), ZPixmap, 0,
                               reinterpret_cast<char*>(pixels.data()), static_cast<unsigned>(new_w),
                               static_cast<unsigned>(new_h), kBitsPerPixel, 0);
  if (!image)
    throw std::runtime_error(
        "smalti: XCreateImage failed (a 24-bit TrueColor display is required)");
  // The layout the server will read must be the core's: 32-bit pixels in this machine's byte
  // order, rows packed. Anything else would put garbage on screen.
  const int native_order = std::endian::native == std::endian::little ? LSBFirst : MSBFirst;
  if (image->bits_per_pixel != kBitsPerPixel || image->byte_order != native_order ||
      image->bytes_per_line != new_w * static_cast<int>(sizeof(uint32_t))) {
    image->data = nullptr;  // the pixels belong to `pixels`, not to Xlib
    XDestroyImage(image);
    throw std::runtime_error(
        "smalti: the X server's image format is not 32-bit pixels in host byte order");
  }
  if (image_) {
    image_->data = nullptr;
    XDestroyImage(image_);
  }
  image_ = image;
  pixels_.swap(pixels);  // the image's data pointer moves with the storage
  width_ = new_w;
  height_ = new_h;
  events_.on_resize(width_, height_, scale_);
}

void X11Window::blit(int x, int y, int w, int h) {
  const RectI r = RectI{x, y, w, h}.intersect({0, 0, width_, height_});
  if (r.empty()) return;
  XPutImage(display_, window_, gc_, image_, r.x, r.y, r.x, r.y, static_cast<unsigned>(r.w),
            static_cast<unsigned>(r.h));
}

void X11Window::present(std::span<const RectI> dirty) {
  for (const RectI& r : dirty) blit(r.x, r.y, r.w, r.h);
  XFlush(display_);
}

void X11Window::run_frame_if_due(int64_t now) {
  if (!requested_ || now < last_frame_ + kFrameNs) return;
  requested_ = false;
  last_frame_ = now;
  events_.on_frame(now);
}

void X11Window::handle(XEvent& e) {
  switch (e.type) {
    case Expose:
      blit(e.xexpose.x, e.xexpose.y, e.xexpose.width, e.xexpose.height);  // our copy is current
      break;
    case ConfigureNotify:
      if (e.xconfigure.width != width_ || e.xconfigure.height != height_)
        resize(e.xconfigure.width, e.xconfigure.height);
      break;
    case MotionNotify:
      events_.on_mouse_move({static_cast<float>(e.xmotion.x), static_cast<float>(e.xmotion.y)},
                            modifiers_of(e.xmotion.state));
      break;
    case LeaveNotify:
      events_.on_mouse_leave();
      break;
    case ButtonPress:
    case ButtonRelease: {
      const bool down = e.type == ButtonPress;
      const Point p{static_cast<float>(e.xbutton.x), static_cast<float>(e.xbutton.y)};
      const Modifiers mods = modifiers_of(e.xbutton.state);
      const unsigned int b = e.xbutton.button;
      if (b >= Button4 && b <= 7) {  // wheel notches arrive as buttons 4 to 7, press only
        if (down) {
          const Point delta = b == Button4   ? Point{0, -kWheelNotchPixels}
                              : b == Button5 ? Point{0, kWheelNotchPixels}
                              : b == 6       ? Point{-kWheelNotchPixels, 0}
                                             : Point{kWheelNotchPixels, 0};
          events_.on_mouse_wheel(p, delta, mods);
        }
        break;
      }
      const MouseButton button = b == Button1   ? MouseButton::left
                                 : b == Button3 ? MouseButton::right
                                                : MouseButton::middle;
      int clicks = 1;
      if (down && button == MouseButton::left) {
        if (e.xbutton.time - last_press_time_ <= kDoubleClickMs &&
            std::abs(e.xbutton.x - last_press_x_) <= kDoubleClickSlop &&
            std::abs(e.xbutton.y - last_press_y_) <= kDoubleClickSlop) {
          clicks = 2;
        }
        last_press_time_ = e.xbutton.time;
        last_press_x_ = e.xbutton.x;
        last_press_y_ = e.xbutton.y;
      }
      events_.on_mouse_button(button, down, p, mods, clicks);
      break;
    }
    case KeyPress:
    case KeyRelease: {
      const bool down = e.type == KeyPress;
      const unsigned int code = e.xkey.keycode;
      const auto held = std::find(keys_down_.begin(), keys_down_.end(), code);
      const bool repeat = down && held != keys_down_.end();
      if (down && !repeat) keys_down_.push_back(code);
      if (!down && held != keys_down_.end()) keys_down_.erase(held);
      const Key k = map_keysym(XLookupKeysym(&e.xkey, 0));
      if (k != Key::unknown) events_.on_key({k, down, repeat, modifiers_of(e.xkey.state)});
      break;
    }
    case FocusIn:
    case FocusOut:
      if (e.type == FocusOut) keys_down_.clear();
      events_.on_focus(e.type == FocusIn);
      break;
    case UnmapNotify:
      // An unmapped (minimised) window loses the pointer grab the server made for a press, and
      // the release may happen elsewhere: the press is over.
      events_.on_capture_lost();
      break;
    case ClientMessage:
      if (static_cast<Atom>(e.xclient.data.l[0]) == backend_.atom("WM_DELETE_WINDOW"))
        events_.on_close_request();
      break;
    default:
      break;
  }
}

// --- X11Backend -----------------------------------------------------------------------------

X11Backend::X11Backend(Display* display, BackendHost& host) : display_(display), host_(host) {
  // The core draws 0xAARRGGBB pixels and hands them to the server as they are, so the default
  // visual must be TrueColor with 8-bit channels in that order (as on every common desktop).
  // 16-bit and 30-bit visuals would need a conversion that does not exist yet.
  const int screen = DefaultScreen(display_);
  const Visual* v = DefaultVisual(display_, screen);
  const int depth = DefaultDepth(display_, screen);
  if (v->c_class != TrueColor || v->red_mask != 0xFF0000 || v->green_mask != 0x00FF00 ||
      v->blue_mask != 0x0000FF || (depth != 24 && depth != 32)) {
    throw std::runtime_error(
        "smalti: the X display's default visual is not 24-bit TrueColor RGB, which is required");
  }
  wake_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (wake_fd_ < 0) throw std::runtime_error("smalti: eventfd failed");
  // Held keys repeat as presses only, not as release-press pairs, so repeats can be told apart.
  XkbSetDetectableAutoRepeat(display_, True, nullptr);
  scale_ = display_scale(display_);
}

X11Backend::~X11Backend() {
  for (auto& [shape, c] : cursors_) XFreeCursor(display_, c);
  close(wake_fd_);
  XCloseDisplay(display_);
}

std::unique_ptr<NativeWindow> X11Backend::create_window(const WindowOptions& options,
                                                        WindowEvents& events) {
  return std::make_unique<X11Window>(*this, options, events);
}

::Cursor X11Backend::cursor(Cursor c) {
  const unsigned int shape = cursor_shape(c);
  for (auto& [s, cur] : cursors_) {
    if (s == shape) return cur;
  }
  const ::Cursor cur = XCreateFontCursor(display_, shape);
  cursors_.emplace_back(shape, cur);
  return cur;
}

void X11Backend::wake() {
  const uint64_t one = 1;
  // A full counter (never in practice) still wakes the loop, so a failed write needs no retry.
  [[maybe_unused]] const ssize_t written = write(wake_fd_, &one, sizeof one);
}

SystemInfo X11Backend::system_info() const {
  SystemInfo info;
  // No portable dark-mode query exists on X11 without D-Bus; GTK_THEME is the common hint.
  if (const char* gtk = std::getenv("GTK_THEME"))
    info.dark_mode = std::strstr(gtk, ":dark") != nullptr;
  return info;
}

void X11Backend::pump_events() {
  while (XPending(display_) > 0) {
    XEvent e;
    XNextEvent(display_, &e);
    for (X11Window* w : windows_) {
      if (w->id() == e.xany.window) {
        w->handle(e);
        break;
      }
    }
  }
}

void X11Backend::run() {
  quit_ = false;
  const int x_fd = ConnectionNumber(display_);
  while (!quit_) {
    pump_events();
    host_.after_events();
    if (quit_) break;
    int64_t due = INT64_MAX;
    for (X11Window* w : windows_) due = std::min(due, w->next_frame_due());
    int timeout_ms = -1;
    if (due != INT64_MAX)
      timeout_ms = static_cast<int>(std::max<int64_t>(0, (due - now_ns() + 999'999) / 1'000'000));
    XFlush(display_);
    pollfd fds[2] = {{x_fd, POLLIN, 0}, {wake_fd_, POLLIN, 0}};
    poll(fds, 2, timeout_ms);
    if (fds[1].revents & POLLIN) {
      uint64_t count = 0;
      [[maybe_unused]] const ssize_t got = read(wake_fd_, &count, sizeof count);
      host_.on_wake();
    }
    pump_events();
    const int64_t now = now_ns();
    const size_t n = windows_.size();
    for (size_t i = 0; i < n && i < windows_.size(); ++i) windows_[i]->run_frame_if_due(now);
    host_.after_events();
  }
}

std::unique_ptr<PlatformBackend> create_x11_backend(BackendHost& host) {
  Display* d = XOpenDisplay(nullptr);
  if (!d) return nullptr;
  try {
    return std::make_unique<X11Backend>(d, host);
  } catch (...) {
    XCloseDisplay(d);  // the backend's destructor, which would close it, never ran
    throw;
  }
}

}  // namespace smalti::platform
