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
| `set_text_input` | switch the input method on or off, and place its windows at the caret | `ImmAssociateContextEx`, `ImmSetCandidateWindow` | `XSetICFocus`, `XNSpotLocation` | `NSTextInputClient` |
| `scale` | device pixels per logical pixel | `GetDpiForWindow / 96` | `Xft.dpi / 96` | `backingScaleFactor` |

## WindowEvents (the backend calls these)

`on_mouse_move`, `on_mouse_button`, `on_mouse_wheel` and `on_mouse_leave` take device pixels; `on_key` takes a `KeyEvent`; `on_text` takes the UTF-8 text typed, after the key events that typed it (the core drops control characters and invalid UTF-8, so pass on what the system gives); `on_focus`, `on_capture_lost` (the system took the mouse capture away; harmless to report after your own release too), `on_resize(width, height, scale)` (also after the frame buffer is replaced), `on_frame(time)` and `on_close_request`.

**Rules a backend must keep:**
- `create_window` reports the initial size through `on_resize` before it returns.
- A window is destroyed by the core, never by the backend; the close button only calls `on_close_request`.
- After each batch of OS events, call `BackendHost::after_events()`, outside any window procedure: that is where closed windows are destroyed.
- End every wait by `BackendHost::next_deadline_ns()` (on the `now_ns()` clock; `INT64_MAX` means no deadline), and on every turn call `BackendHost::run_timers(now)` before running frames. That is how timed redraws and the pause before a theme reloads work without polling. If the platform runs modal loops of its own that keep frames coming, call it there too.
- `wake()` is the only call that may come from another thread. Where the platform has modal loops of its own (Windows moving a window, a native message box), deliver the wake so that those loops run it too; on Windows it is a message to a message-only window.
- When the frame buffer is replaced (a resize), make the new one before releasing the old: the core draws into the old one until `on_resize` hands it the new one. If making it fails, report `on_resize` with an empty buffer before letting the error out.
- Register a window with the backend only once it is completely built. If its constructor throws, the destructor does not run: release what was already created (on Windows, also detach the object from its window handle) before rethrowing.
- Nothing may throw through the operating system's callbacks.

## PlatformBackend

`create_window`, `run` (returns after `quit`), `quit`, `wake`, `now_ns` (a monotonic clock), `system_info` (dark mode, reduced motion), and `watch_file` and `stop_watching_file` for live reload: add the watch's handle or descriptor to the loop's wait, and call the callback when it reports the file (`src/platform/file_watch.hpp` has a class for Windows and Linux). Register the backend in `src/platform/backend.cpp` and its source file in `CMakeLists.txt`.
