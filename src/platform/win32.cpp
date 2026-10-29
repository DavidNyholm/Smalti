// The Win32 backend: windows, input, DPI, the event loop, and presentation. The first frames
// go through GDI (no GPU device yet); a background thread creates the Direct3D 11 device and,
// once it exists, every window moves to the GPU compositor.
#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "platform/platform.hpp"
#include "platform/win32_gpu.hpp"

namespace smalti::platform {

namespace {

constexpr wchar_t kWindowClass[] = L"smalti.window";
constexpr wchar_t kWakeClass[] = L"smalti.wake";
constexpr UINT kWakeMessage = WM_APP + 1;
// MsgWaitForMultipleObjectsEx takes at most this many handles (one fewer than the usual limit:
// the message queue counts as one).
constexpr size_t kMaxWaitHandles = MAXIMUM_WAIT_OBJECTS - 1;
// Messages handled per turn of the loop before frames get theirs: a stream of posted messages
// (a long job posting its next piece) must not keep windows from drawing.
constexpr int kMaxMessagesPerTurn = 64;
constexpr float kDefaultDpi = 96.0f;
constexpr UINT_PTR kModalLoopTimer = 1;  // keeps frames coming while a window is dragged
constexpr double kFallbackRefreshHz = 60.0;
constexpr float kWheelLinePixels = 40.0f;       // one wheel notch of one line, in logical pixels
constexpr DWORD kDwmUseImmersiveDarkMode = 20;  // DWMWA_USE_IMMERSIVE_DARK_MODE

std::wstring widen(std::string_view utf8) {
  if (utf8.empty()) return {};
  const int n =
      MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring out(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
  return out;
}

int64_t qpc_ns() {
  static const int64_t freq = [] {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return f.QuadPart;
  }();
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  // Split to avoid overflow: whole seconds, then the remainder.
  return (t.QuadPart / freq) * 1'000'000'000 + (t.QuadPart % freq) * 1'000'000'000 / freq;
}

Modifiers current_modifiers() {
  return {(GetKeyState(VK_SHIFT) & 0x8000) != 0, (GetKeyState(VK_CONTROL) & 0x8000) != 0,
          (GetKeyState(VK_MENU) & 0x8000) != 0,
          ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) != 0};
}

Key map_key(WPARAM vk) {
  if (vk >= 'A' && vk <= 'Z')
    return static_cast<Key>(static_cast<int>(Key::a) + static_cast<int>(vk - 'A'));
  if (vk >= '0' && vk <= '9')
    return static_cast<Key>(static_cast<int>(Key::digit0) + static_cast<int>(vk - '0'));
  if (vk >= VK_F1 && vk <= VK_F12)
    return static_cast<Key>(static_cast<int>(Key::f1) + static_cast<int>(vk - VK_F1));
  switch (vk) {
    case VK_SPACE:
      return Key::space;
    case VK_RETURN:
      return Key::enter;
    case VK_ESCAPE:
      return Key::escape;
    case VK_TAB:
      return Key::tab;
    case VK_BACK:
      return Key::backspace;
    case VK_DELETE:
      return Key::delete_;
    case VK_LEFT:
      return Key::left;
    case VK_RIGHT:
      return Key::right;
    case VK_UP:
      return Key::up;
    case VK_DOWN:
      return Key::down;
    case VK_HOME:
      return Key::home;
    case VK_END:
      return Key::end;
    case VK_PRIOR:
      return Key::page_up;
    case VK_NEXT:
      return Key::page_down;
    default:
      return Key::unknown;
  }
}

LPCWSTR cursor_resource(Cursor c) {
  switch (c) {
    case Cursor::hand:
      return IDC_HAND;
    case Cursor::text:
      return IDC_IBEAM;
    case Cursor::resize_horizontal:
      return IDC_SIZEWE;
    case Cursor::resize_vertical:
      return IDC_SIZENS;
    case Cursor::arrow:
      break;
  }
  return IDC_ARROW;
}

// DWM is not on the KnownDLLs list, so it is loaded from the system directory explicitly.
using DwmSetAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
DwmSetAttributeFn dwm_set_attribute() {
  static DwmSetAttributeFn fn = [] {
    const HMODULE m = LoadLibraryExW(L"dwmapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    return m ? reinterpret_cast<DwmSetAttributeFn>(GetProcAddress(m, "DwmSetWindowAttribute"))
             : nullptr;
  }();
  return fn;
}

double refresh_hz(HWND hwnd) {
  MONITORINFOEXW info{};
  info.cbSize = sizeof info;
  if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &info))
    return kFallbackRefreshHz;
  DEVMODEW mode{};
  mode.dmSize = sizeof mode;
  if (!EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) ||
      mode.dmDisplayFrequency <= 1) {
    return kFallbackRefreshHz;
  }
  return mode.dmDisplayFrequency;
}

}  // namespace

class Win32Backend;

class Win32Window final : public NativeWindow {
 public:
  Win32Window(Win32Backend& backend, const WindowOptions& options, WindowEvents& events);
  ~Win32Window() override;

  void show() override { ShowWindow(hwnd_, SW_SHOW); }
  void set_title(std::string_view utf8) override { SetWindowTextW(hwnd_, widen(utf8).c_str()); }
  PixelBuffer frame_buffer() override;
  void present(std::span<const RectI> dirty) override;
  void request_frame() override;
  bool frame_requested() const override { return requested_; }
  void set_cursor(Cursor c) override;
  void capture_mouse(bool capture) override;
  void set_dark_frame(bool dark) override;
  float scale() const override { return scale_; }
  size_t gpu_bytes() const override { return gpu_ ? gpu_->bytes() : 0; }

  // Handles a message; anything thrown is stopped here and rethrown from run(), so nothing
  // unwinds through user32.
  LRESULT dispatch(UINT msg, WPARAM wp, LPARAM lp);
  void attach(HWND hwnd) { hwnd_ = hwnd; }  // set while CreateWindowExW is still running
  // Runs the requested frame if this window's pacing allows it now.
  void run_frame_if_due(int64_t now);
  // The event loop's wait consumed this window's swap-chain signal: the display can take a
  // frame. The permission lasts until a frame is actually presented; a frame that turns out to
  // have nothing to draw keeps it, or the window would wait for a signal that never comes.
  void mark_gpu_ready() { gpu_ready_ = true; }
  bool waiting_for_gpu() const { return requested_ && gpu_ && !gpu_ready_; }
  HANDLE gpu_waitable() const { return gpu_ ? gpu_->frame_waitable() : nullptr; }
  // When the requested frame may run: INT64_MAX when none is requested or it waits for the
  // swap chain's signal (waited on separately), 0 when it may run now.
  int64_t next_frame_due() const {
    if (!requested_) return INT64_MAX;
    if (gpu_) return gpu_ready_ ? 0 : INT64_MAX;
    return last_frame_ + period_ns();
  }
  int64_t period_ns() const { return static_cast<int64_t>(1e9 / refresh_hz_); }
  void use_gpu(GpuDevice& device);
  HWND hwnd() const { return hwnd_; }

 private:
  LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
  void release();  // frees whatever exists so far; the destructor, or a failed constructor
  void resize_buffers(int w, int h);
  void make_dib(int w, int h);
  void drop_buffers();  // after a failed resize: no frame buffer until the next one succeeds
  void run_frame(int64_t now);

  Win32Backend& backend_;
  WindowEvents& events_;
  HWND hwnd_ = nullptr;
  Size min_size_;
  float scale_ = 1;
  int width_ = 0, height_ = 0;
  double refresh_hz_ = kFallbackRefreshHz;
  bool requested_ = false;
  int64_t last_frame_ = 0;
  bool in_modal_loop_ = false;
  bool gpu_ready_ = false;
  bool tracking_leave_ = false;
  HCURSOR cursor_ = nullptr;

  // The CPU path: a DIB the core draws into, copied to the window with GDI.
  HDC mem_dc_ = nullptr;
  HBITMAP dib_ = nullptr;
  HGDIOBJ old_bitmap_ = nullptr;
  PixelBuffer dib_pixels_;
  // The GPU path, once the device exists.
  std::unique_ptr<GpuSurface> gpu_;
};

class Win32Backend final : public PlatformBackend {
 public:
  Win32Backend(Presentation presentation, BackendHost& host);
  ~Win32Backend() override;

  std::unique_ptr<NativeWindow> create_window(const WindowOptions& options,
                                              WindowEvents& events) override;
  void run() override;
  void quit() override { quit_ = true; }
  void wake() override;
  int64_t now_ns() const override { return qpc_ns(); }
  SystemInfo system_info() const override;

  void add(Win32Window* w) { windows_.push_back(w); }
  void forget(Win32Window* w) { std::erase(windows_, w); }
  GpuDevice* gpu() { return gpu_.get(); }
  void capture(std::exception_ptr e) {
    if (!error_) error_ = e;
    quit_ = true;
  }
  // Runs every window's frame that is due; also from inside Windows' own modal loops.
  void run_due_frames();
  void on_wake_message();
  void run_lost_wake();  // runs posted work whose wake message could not be posted

 private:
  void adopt_gpu_if_ready();
  void rethrow_error();  // if a backend failure is pending
  void dispatch_messages(UINT filter);

  BackendHost& host_;
  // Wakes arrive as a message to this message-only window, not as an event the loop waits on:
  // Windows' own modal loops (moving, resizing, menus, message boxes) dispatch messages, so
  // posted work keeps running while they spin.
  HWND wake_window_ = nullptr;
  std::atomic<bool> wake_posted_{false};
  std::atomic<bool> wake_lost_{false};  // PostMessage failed (a full queue): the loop runs the work
  // Signalled with wake_lost_, and part of every wait: unlike a message it cannot fail to be
  // delivered, so the loop cannot go to sleep between a failed post and the flag being set.
  HANDLE lost_wake_event_ = nullptr;
  HANDLE frame_timer_ = nullptr;  // high resolution: paces CPU-path frames without spinning
  std::vector<Win32Window*> windows_;
  bool quit_ = false;
  std::exception_ptr error_;

  std::thread gpu_thread_;  // one-shot: creates the device, then exits
  std::atomic<bool> gpu_done_{false};
  std::unique_ptr<GpuDevice> gpu_pending_;
  std::unique_ptr<GpuDevice> gpu_;
};

LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
    auto* created = static_cast<Win32Window*>(cs->lpCreateParams);
    created->attach(hwnd);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(created));
  }
  auto* w = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  return w ? w->dispatch(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK wake_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == kWakeMessage) {
    if (auto* b = reinterpret_cast<Win32Backend*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)))
      b->on_wake_message();
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

// --- Win32Window ----------------------------------------------------------------------------

Win32Window::Win32Window(Win32Backend& backend, const WindowOptions& options, WindowEvents& events)
    : backend_(backend), events_(events), min_size_(options.min_size) {
  cursor_ = LoadCursorW(nullptr, IDC_ARROW);
  hwnd_ = CreateWindowExW(0, kWindowClass, widen(options.title).c_str(), WS_OVERLAPPEDWINDOW,
                          CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr,
                          nullptr, GetModuleHandleW(nullptr), this);
  if (!hwnd_) throw std::runtime_error("smalti: CreateWindowExW failed");
  try {
    // The size asked for is the client area in logical pixels, on whichever monitor the window
    // landed on.
    const UINT dpi = GetDpiForWindow(hwnd_);
    scale_ = static_cast<float>(dpi) / kDefaultDpi;
    RECT r{0, 0, static_cast<LONG>(std::lround(options.size.width * scale_)),
           static_cast<LONG>(std::lround(options.size.height * scale_))};
    AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);
    SetWindowPos(hwnd_, nullptr, 0, 0, r.right - r.left, r.bottom - r.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    refresh_hz_ = refresh_hz(hwnd_);
    RECT client;
    GetClientRect(hwnd_, &client);
    resize_buffers(client.right, client.bottom);
    if (backend_.gpu()) use_gpu(*backend_.gpu());
    backend_.add(this);  // last: the backend never sees a half-made window
  } catch (...) {
    release();  // the destructor will not run, and the HWND still points at this object
    throw;
  }
}

Win32Window::~Win32Window() {
  backend_.forget(this);
  release();
}

void Win32Window::release() {
  SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);  // no more messages reach this object
  gpu_.reset();
  DestroyWindow(hwnd_);
  if (mem_dc_) {
    SelectObject(mem_dc_, old_bitmap_);
    DeleteDC(mem_dc_);
    mem_dc_ = nullptr;
  }
  if (dib_) DeleteObject(dib_);
  dib_ = nullptr;
}

void Win32Window::make_dib(int w, int h) {
  if (!mem_dc_) {
    mem_dc_ = CreateCompatibleDC(nullptr);
    old_bitmap_ = nullptr;
    if (!mem_dc_) throw std::runtime_error("smalti: CreateCompatibleDC failed");
  }
  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof bmi.bmiHeader;
  bmi.bmiHeader.biWidth = std::max(1, w);
  bmi.bmiHeader.biHeight = -std::max(1, h);  // top-down rows, like the core's
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  // The new bitmap first: if it cannot be made, the old one stays, and so do the pixels the
  // core is drawing into.
  HBITMAP fresh = CreateDIBSection(mem_dc_, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!fresh) throw std::runtime_error("smalti: CreateDIBSection failed");
  if (dib_) {
    SelectObject(mem_dc_, old_bitmap_);
    DeleteObject(dib_);
  }
  dib_ = fresh;
  old_bitmap_ = SelectObject(mem_dc_, dib_);
  dib_pixels_ = {static_cast<uint32_t*>(bits), std::max(1, w), std::max(1, h), std::max(1, w)};
}

void Win32Window::resize_buffers(int w, int h) {
  width_ = std::max(1, w);
  height_ = std::max(1, h);
  try {
    if (gpu_ && !gpu_->resize(width_, height_)) gpu_.reset();  // device lost: back to the CPU path
    if (!gpu_) make_dib(width_, height_);
  } catch (...) {
    // A buffer may already be gone: the core must learn that it has none, not keep drawing
    // into freed pixels.
    drop_buffers();
    events_.on_resize(width_, height_, scale_);
    throw;
  }
  events_.on_resize(width_, height_, scale_);
}

void Win32Window::drop_buffers() {
  gpu_.reset();
  if (dib_) {
    SelectObject(mem_dc_, old_bitmap_);
    DeleteObject(dib_);
    dib_ = nullptr;
  }
  dib_pixels_ = {};
}

void Win32Window::use_gpu(GpuDevice& device) {
  if (gpu_) return;
  gpu_ = GpuSurface::create(device, hwnd_, width_, height_);
  if (!gpu_) return;
  gpu_ready_ = false;  // a new swap chain signals its first frame itself
  // The DIB is no longer needed; the core redraws everything into the GPU path's buffer.
  if (dib_) {
    SelectObject(mem_dc_, old_bitmap_);
    DeleteObject(dib_);
    dib_ = nullptr;
  }
  events_.on_resize(width_, height_, scale_);
}

PixelBuffer Win32Window::frame_buffer() { return gpu_ ? gpu_->frame_buffer() : dib_pixels_; }

void Win32Window::present(std::span<const RectI> dirty) {
  if (gpu_) {
    gpu_ready_ = false;  // this Present spends the permission; the waitable signals the next
    if (gpu_->present(dirty)) return;
    gpu_.reset();  // the device was lost: fall back to GDI and draw everything again
    resize_buffers(width_, height_);
    return;
  }
  HDC dc = GetDC(hwnd_);
  for (const RectI& r : dirty) BitBlt(dc, r.x, r.y, r.w, r.h, mem_dc_, r.x, r.y, SRCCOPY);
  ReleaseDC(hwnd_, dc);
}

void Win32Window::request_frame() { requested_ = true; }

void Win32Window::run_frame(int64_t now) {
  if (!requested_) return;
  requested_ = false;
  last_frame_ = now;
  events_.on_frame(now);
}

void Win32Window::run_frame_if_due(int64_t now) {
  if (!requested_) return;
  if (gpu_) {
    // The swap chain's waitable object says the display can take a frame. Waiting on it uses
    // up the signal, so a signal the event loop already consumed is remembered separately.
    if (gpu_ready_ || WaitForSingleObject(gpu_->frame_waitable(), 0) == WAIT_OBJECT_0) {
      gpu_ready_ = true;  // spent by present(), not here: the frame may draw nothing
      run_frame(now);
    }
  } else if (now >= last_frame_ + period_ns()) {
    run_frame(now);
  }
}

void Win32Window::set_cursor(Cursor c) {
  cursor_ = LoadCursorW(nullptr, cursor_resource(c));
  POINT p;
  RECT r;
  if (GetCursorPos(&p) && ScreenToClient(hwnd_, &p) && GetClientRect(hwnd_, &r) && PtInRect(&r, p))
    SetCursor(cursor_);
}

void Win32Window::capture_mouse(bool capture) {
  if (capture)
    SetCapture(hwnd_);
  else if (GetCapture() == hwnd_)
    ReleaseCapture();
}

void Win32Window::set_dark_frame(bool dark) {
  if (auto fn = dwm_set_attribute()) {
    const BOOL value = dark ? TRUE : FALSE;
    fn(hwnd_, kDwmUseImmersiveDarkMode, &value, sizeof value);
  }
}

LRESULT Win32Window::dispatch(UINT msg, WPARAM wp, LPARAM lp) {
  try {
    return handle(msg, wp, lp);
  } catch (...) {
    // The core already guards app callbacks; this catches the backend's own failures.
    backend_.capture(std::current_exception());
    return DefWindowProcW(hwnd_, msg, wp, lp);
  }
}

LRESULT Win32Window::handle(UINT msg, WPARAM wp, LPARAM lp) {
  const auto device_point = [&] {
    return Point{static_cast<float>(GET_X_LPARAM(lp)), static_cast<float>(GET_Y_LPARAM(lp))};
  };
  switch (msg) {
    case WM_MOUSEMOVE:
      if (!tracking_leave_) {
        TRACKMOUSEEVENT tme{sizeof tme, TME_LEAVE, hwnd_, 0};
        tracking_leave_ = TrackMouseEvent(&tme) != FALSE;
      }
      events_.on_mouse_move(device_point(), current_modifiers());
      return 0;
    case WM_MOUSELEAVE:
      tracking_leave_ = false;
      events_.on_mouse_leave();
      return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN: {
      const MouseButton b = msg == WM_RBUTTONDOWN   ? MouseButton::right
                            : msg == WM_MBUTTONDOWN ? MouseButton::middle
                                                    : MouseButton::left;
      events_.on_mouse_button(b, true, device_point(), current_modifiers(),
                              msg == WM_LBUTTONDBLCLK ? 2 : 1);
      return 0;
    }
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP: {
      const MouseButton b = msg == WM_RBUTTONUP   ? MouseButton::right
                            : msg == WM_MBUTTONUP ? MouseButton::middle
                                                  : MouseButton::left;
      events_.on_mouse_button(b, false, device_point(), current_modifiers(), 1);
      return 0;
    }
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL: {
      POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};  // screen coordinates for wheel messages
      ScreenToClient(hwnd_, &p);
      UINT lines = 3;
      SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
      const float notches = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA;
      const float d = notches * static_cast<float>(lines) * kWheelLinePixels;
      // Windows reports wheel-up as positive; smalti's positive is down (content moves up).
      const Point delta = msg == WM_MOUSEWHEEL ? Point{0, -d} : Point{d, 0};
      events_.on_mouse_wheel({static_cast<float>(p.x), static_cast<float>(p.y)}, delta,
                             current_modifiers());
      return 0;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP: {
      const bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
      const Key k = map_key(wp);
      if (k != Key::unknown)
        events_.on_key({k, down, down && (lp & (1 << 30)) != 0, current_modifiers()});
      if (msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP)
        break;  // Alt+F4 and the system menu still work
      return 0;
    }
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
      events_.on_focus(msg == WM_SETFOCUS);
      return 0;
    case WM_SIZE:
      if (wp != SIZE_MINIMIZED) {
        resize_buffers(LOWORD(lp), HIWORD(lp));
        if (in_modal_loop_) run_frame(backend_.now_ns());  // live resize: draw before returning
      }
      return 0;
    case WM_DPICHANGED: {
      scale_ = static_cast<float>(HIWORD(wp)) / kDefaultDpi;
      const RECT* r = reinterpret_cast<const RECT*>(lp);
      SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      refresh_hz_ = refresh_hz(hwnd_);
      return 0;
    }
    case WM_DISPLAYCHANGE:
    case WM_MOVE:
      refresh_hz_ = refresh_hz(hwnd_);  // a move may land the window on another monitor
      break;
    case WM_GETMINMAXINFO: {
      auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
      RECT r{0, 0, static_cast<LONG>(min_size_.width * scale_),
             static_cast<LONG>(min_size_.height * scale_)};
      AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0,
                               static_cast<UINT>(scale_ * kDefaultDpi));
      mm->ptMinTrackSize = {r.right - r.left, r.bottom - r.top};
      return 0;
    }
    case WM_ENTERSIZEMOVE:
      // Windows runs its own message loop while a window is moved or resized; our loop stops,
      // so a timer keeps animation frames coming inside theirs.
      in_modal_loop_ = true;
      SetTimer(hwnd_, kModalLoopTimer, USER_TIMER_MINIMUM, nullptr);
      return 0;
    case WM_EXITSIZEMOVE:
      in_modal_loop_ = false;
      KillTimer(hwnd_, kModalLoopTimer);
      return 0;
    case WM_TIMER:
      if (wp == kModalLoopTimer) {
        backend_.run_lost_wake();
        backend_.run_due_frames();  // every window, not just this one
      }
      return 0;
    case WM_CAPTURECHANGED:
      events_.on_capture_lost();  // also after our own release; the core ignores that one
      return 0;
    case WM_SETCURSOR:
      if (LOWORD(lp) == HTCLIENT) {
        SetCursor(cursor_);
        return TRUE;
      }
      break;
    case WM_ERASEBKGND:
      return 1;  // the whole client area is always drawn by us
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(hwnd_, &ps);
      if (!gpu_ && dib_) {
        const RECT& r = ps.rcPaint;
        BitBlt(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, mem_dc_, r.left, r.top,
               SRCCOPY);
      }
      EndPaint(hwnd_, &ps);
      return 0;
    }
    case WM_CLOSE:
      events_.on_close_request();  // the core decides; it closes the window by destroying us
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd_, msg, wp, lp);
}

// --- Win32Backend ---------------------------------------------------------------------------

Win32Backend::Win32Backend(Presentation presentation, BackendHost& host) : host_(host) {
  // Per-monitor DPI awareness (v2) before any window exists. If the application's manifest has
  // already set an awareness, this fails harmlessly and the manifest wins.
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  WNDCLASSEXW wc{sizeof wc};
  wc.style = CS_DBLCLKS;
  wc.lpfnWndProc = window_proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = kWindowClass;
  RegisterClassExW(&wc);  // fails harmlessly if a previous App registered it
  WNDCLASSEXW wake_class{sizeof wake_class};
  wake_class.lpfnWndProc = wake_proc;
  wake_class.hInstance = wc.hInstance;
  wake_class.lpszClassName = kWakeClass;
  RegisterClassExW(&wake_class);

  frame_timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                        TIMER_ALL_ACCESS);
  if (!frame_timer_)
    frame_timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);  // before Windows 10 1803
  wake_window_ = CreateWindowExW(0, kWakeClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                 wc.hInstance, nullptr);
  lost_wake_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!frame_timer_ || !wake_window_ || !lost_wake_event_) {
    if (frame_timer_) CloseHandle(frame_timer_);
    if (wake_window_) DestroyWindow(wake_window_);
    if (lost_wake_event_) CloseHandle(lost_wake_event_);
    throw std::runtime_error("smalti: cannot create the event loop's handles");
  }
  SetWindowLongPtrW(wake_window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

  if (presentation == Presentation::automatic) {
    // The device costs hundreds of milliseconds on some drivers; the first frames do not wait.
    gpu_thread_ = std::thread([this] {
      gpu_pending_ = GpuDevice::create();
      gpu_done_ = true;
      wake();
    });
  }
}

Win32Backend::~Win32Backend() {
  if (gpu_thread_.joinable()) gpu_thread_.join();
  DestroyWindow(wake_window_);  // a wake still queued for it is dropped with it
  CloseHandle(frame_timer_);
  CloseHandle(lost_wake_event_);
}

void Win32Backend::wake() {
  // One message in the queue is enough however many wakes there are; on_wake drains all work.
  if (wake_posted_.exchange(true)) return;
  if (!PostMessageW(wake_window_, kWakeMessage, 0, 0)) {
    // A full queue. The work must not wait for another post that may never come: the loop
    // (and the modal-loop timer) checks this flag after the messages it is about to handle.
    wake_posted_ = false;
    wake_lost_ = true;
    SetEvent(lost_wake_event_);
  }
}

void Win32Backend::run_lost_wake() {
  if (wake_lost_.exchange(false)) on_wake_message();
}

void Win32Backend::on_wake_message() {
  wake_posted_ = false;  // before draining: work posted from now on needs a new message
  try {
    adopt_gpu_if_ready();
    host_.on_wake();
  } catch (...) {
    capture(std::current_exception());
  }
}

std::unique_ptr<NativeWindow> Win32Backend::create_window(const WindowOptions& options,
                                                          WindowEvents& events) {
  adopt_gpu_if_ready();
  return std::make_unique<Win32Window>(*this, options, events);
}

SystemInfo Win32Backend::system_info() const {
  SystemInfo info;
  DWORD light = 1, size = sizeof light;
  if (RegGetValueW(
          HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
          L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size) == ERROR_SUCCESS) {
    info.dark_mode = light == 0;
  }
  BOOL animations = TRUE;
  if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0))
    info.reduced_motion = !animations;
  return info;
}

void Win32Backend::adopt_gpu_if_ready() {
  if (gpu_ || !gpu_done_) return;
  if (gpu_thread_.joinable()) gpu_thread_.join();
  gpu_ = std::move(gpu_pending_);
  if (!gpu_) return;  // no Direct3D 11 here: stay on the CPU path
  for (Win32Window* w : windows_) w->use_gpu(*gpu_);
}

void Win32Backend::run_due_frames() {
  if (error_) return;  // a window's buffers may be gone; run() rethrows the error instead
  const int64_t now = now_ns();
  // By index: closed windows are destroyed only in after_events, so the list cannot shrink
  // during these frames.
  const size_t count = windows_.size();
  for (size_t i = 0; i < count && i < windows_.size(); ++i) windows_[i]->run_frame_if_due(now);
}

void Win32Backend::rethrow_error() {
  if (!error_) return;
  std::exception_ptr e = error_;
  error_ = nullptr;
  std::rethrow_exception(e);
}

void Win32Backend::dispatch_messages(UINT filter) {
  MSG msg;
  for (int n = 0; n < kMaxMessagesPerTurn && PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE | filter);
       ++n) {
    if (msg.message == WM_QUIT) quit_ = true;
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
}

void Win32Backend::run() {
  // A failure reported before the loop starts (a window whose construction failed in a message
  // handler) comes out now: the loop would skip frames for it without ever ending.
  rethrow_error();
  quit_ = false;
  std::vector<HANDLE> handles;
  std::vector<Win32Window*> owners;  // the window behind each handle, or nullptr
  while (!quit_) {
    // Wait for: a message (input, or a wake), the CPU-path frame timer, or a GPU window's swap
    // chain ready for its frame.
    handles.assign(1, lost_wake_event_);
    owners.assign(1, nullptr);
    const int64_t now = now_ns();
    int64_t due = INT64_MAX;
    for (Win32Window* w : windows_) {
      if (!w->waiting_for_gpu()) {
        due = std::min(due, w->next_frame_due());
      } else if (handles.size() + 1 < kMaxWaitHandles) {  // one place stays for the timer
        handles.push_back(w->gpu_waitable());
        owners.push_back(w);
      } else {
        // More windows than the wait can take: poll this one at its refresh rate.
        due = std::min(due, now + w->period_ns());
      }
    }
    DWORD timeout = INFINITE;
    if (due <= now) {
      timeout = 0;  // a frame may run already: just collect what is waiting
    } else if (due != INT64_MAX) {
      LARGE_INTEGER rel;
      rel.QuadPart = -std::max<int64_t>(1, (due - now) / 100);  // relative, in 100 ns units
      SetWaitableTimer(frame_timer_, &rel, 0, nullptr, nullptr, FALSE);
      handles.push_back(frame_timer_);
      owners.push_back(nullptr);
    }
    const DWORD r = MsgWaitForMultipleObjectsEx(static_cast<DWORD>(handles.size()), handles.data(),
                                                timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    if (r == WAIT_FAILED) {
      // Not retried: a wait that fails once fails every time, and retrying would spin.
      capture(std::make_exception_ptr(
          std::runtime_error("smalti: MsgWaitForMultipleObjectsEx failed (error " +
                             std::to_string(GetLastError()) + ")")));
      break;
    }
    if (r < WAIT_OBJECT_0 + handles.size() && owners[r - WAIT_OBJECT_0])
      owners[r - WAIT_OBJECT_0]->mark_gpu_ready();
    // Input first: the queue hands out posted messages (wakes among them) before mouse and
    // keyboard input, so a job that keeps posting would otherwise keep input waiting.
    dispatch_messages(PM_QS_INPUT);
    dispatch_messages(0);
    run_lost_wake();
    run_due_frames();
    host_.after_events();
  }
  if (error_) {
    std::exception_ptr e = error_;
    error_ = nullptr;
    std::rethrow_exception(e);
  }
}

std::unique_ptr<PlatformBackend> create_win32_backend(Presentation presentation,
                                                      BackendHost& host) {
  return std::make_unique<Win32Backend>(presentation, host);
}

}  // namespace smalti::platform
