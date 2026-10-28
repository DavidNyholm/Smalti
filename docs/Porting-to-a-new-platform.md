# Porting to a new platform

A platform is two classes in `src/platform/platform.hpp`: `PlatformBackend` (the event loop) and `NativeWindow` (one window). The core calls them and receives events through `WindowEvents` and `BackendHost`. Nothing else in smalti knows which platform is running; `headless.cpp` is the shortest complete implementation to start from.

## NativeWindow

| Function | What it must do | Win32 | X11 | Cocoa (for a macOS port) |
|---|---|---|---|---|
| `show` | make the window visible | `ShowWindow` | `XMapWindow` | `makeKeyAndOrderFront` |
| `set_title` | UTF-8 title | `SetWindowTextW` | `_NET_WM_NAME` | `setTitle:` |
| `frame_buffer` | 32-bit pixels the core draws into (0xAARRGGBB) | a DIB section, or a CPU bitmap on the GPU path | an `XImage` over our own memory | a bitmap for a `CALayer` |
| `present` | put the given rectangles on screen | `BitBlt`, or upload and `Present` | `XPutImage` | set the layer contents |
| `request_frame` / `frame_requested` | one `on_frame` call at the next moment the display can take a frame | waitable timer or DXGI waitable | `poll` timeout | `CVDisplayLink` or `CADisplayLink` |
| `set_cursor` | arrow, hand, text, resize | `SetCursor` | `XDefineCursor` | `NSCursor set` |
| `capture_mouse` | keep mouse events while a button is down | `SetCapture` | implicit grab | automatic |
| `set_dark_frame` | dark title bar, where supported | DWM attribute 20 | `_GTK_THEME_VARIANT` | `NSAppearance` |
| `scale` | device pixels per logical pixel | `GetDpiForWindow / 96` | `Xft.dpi / 96` | `backingScaleFactor` |

## WindowEvents (the backend calls these)

`on_mouse_move`, `on_mouse_button`, `on_mouse_wheel` and `on_mouse_leave` take device pixels; `on_key` takes a `KeyEvent`; `on_focus`, `on_resize(width, height, scale)` (also after the frame buffer is replaced), `on_frame(time)` and `on_close_request`.

**Rules a backend must keep:**
- `create_window` reports the initial size through `on_resize` before it returns.
- A window is destroyed by the core, never by the backend; the close button only calls `on_close_request`.
- After each batch of OS events, call `BackendHost::after_events()`, outside any window procedure: that is where closed windows are destroyed.
- `wake()` is the only call that may come from another thread.
- Nothing may throw through the operating system's callbacks.

## PlatformBackend

`create_window`, `run` (returns after `quit`), `quit`, `wake`, `now_ns` (a monotonic clock), and `system_info` (dark mode, reduced motion). Register the backend in `src/platform/backend.cpp` and its source file in `CMakeLists.txt`.
