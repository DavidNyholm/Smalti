# Events and threading

## One UI thread

All of smalti runs on the thread that created the `App`. Every call through a `Ref` checks that it is on that thread; a call from another thread is a reported contract violation naming `App::post`, instead of a race that shows up a week later.

`app.post(fn)` is the only thread-safe call. It queues `fn` and wakes the loop, which runs it on the UI thread:

```cpp
std::thread worker([&app, status] {
  auto result = compute();  // slow work off the UI thread
  app.post([status, result] { status->set_text(result); });
});
```

## The loop

`app.run()` sleeps in the operating system (`MsgWaitForMultipleObjectsEx` on Windows, `poll` on Linux) until input, a requested frame, posted work, or the time of a timed redraw arrives. There is no polling and no frame loop: a window that is not animating draws nothing and uses no CPU. `run()` returns when the last window closes or `app.quit()` is called. Calling `run()` again while it is running is a contract violation.

Posted work runs in the order it was posted. On Windows it arrives as a window message, so it also runs while the system's own loops are spinning: while a window is moved or resized, a menu is open, or a message box is up. A posted function that itself runs such a loop holds back later posted work until it returns, so the order is never broken.

## Callbacks

Each event has one callback, set with `on_click`, `on_change`, `on_select`. Callbacks are move-only (`smalti::Callback`), so they may own what they capture.

- **A callback may remove its own widget, its parent, or close its window.** Removal happens once the outermost event has been handled, so no code ever runs inside a destroyed object. That holds when a callback runs a nested event loop (a native message box) too: events handled inside it cannot destroy a widget whose handler is still on the stack.
- **Widgets are added from events and posted functions,** not from `measure()`, `arrange()` or `paint()`: adding while the window lays out or paints is a contract violation.
- **A callback may replace itself.** `button->on_click(...)` from inside the button's own click handler is fine; the running handler stays alive until it returns.
- **An exception thrown by a callback** stops the loop and is rethrown from `app.run()`. It never unwinds through the operating system.

## Mouse presses and focus

A press goes to the widget under the pointer and stays with it until that button is released, wherever the pointer goes. There is one press at a time: other buttons are ignored until it ends. If the widget is hidden, disabled or removed first (by its own press handler too), or the system takes the mouse away (another window grabs it, or the window is minimised), the press ends with `MouseEvent::Kind::cancel` instead of a release: undo whatever the press started, and do not act on it.

A widget that is hidden or disabled, or sits inside a container that is, loses keyboard focus, and keys never reach it.

## Typed text and the input method

Text the user types reaches the focused widget through `on_text(utf8)`, separately from keys: a key may type nothing (Shift, the arrows), and one character may take several keys, through a dead key (´ then e for é), a compose sequence, or an input method such as the Japanese or Chinese IMEs, which may commit a whole word at once. The text comes after the key events that typed it. It never holds control characters: Tab, Enter, Backspace, Escape and Ctrl combinations arrive only as keys, and invalid UTF-8 is dropped before a widget sees it.

A widget that takes text says so with `accepts_text()`, and gives the position of its caret with `text_caret()` (calling `text_caret_moved()` when it moves). While such a widget has focus, the window's input method is switched on and its windows (the text being composed, the list of candidates) are placed at the caret; while anything else has focus it is off, so an IME never opens over a button. Text still being composed when the focus moves away is discarded rather than given to the next widget.

- **Windows:** `WM_CHAR`, with the two halves of characters outside the Basic Multilingual Plane (most emoji) joined. The input context is attached only for a widget that takes text, and the IME's windows are placed with `ImmSetCompositionWindow` and `ImmSetCandidateWindow`, the candidates kept clear of the caret.
- **Linux:** XIM, which every X11 input method serves (IBus and Fcitx among them, through `XMODIFIERS`). Without a running one, Xlib's own input method is used, so dead keys and compose sequences still type. Where the input method offers it, what is being composed appears at the caret (over the spot); otherwise it composes in a window of its own. Xlib reads typed text in the C library's locale, so if the program left `LC_CTYPE` as "C", smalti takes it from the environment (`LANG`), as other toolkits do; a locale the program chose is left alone.
- Showing the text being composed inside the widget itself (inline composition) is planned after v0.5.

## Frames and motion

Widgets mark what changed (`invalidate()`); the window redraws only those rectangles at its next frame, in step with the display. Animations are values on the frame clock (`animate(motion, target, duration)`, read with `at(motion)`), so they cost nothing between animations and show the right value at any frame rate. With the system's animation setting turned off, they jump straight to the end.

A widget that changes at a later time without input, such as a blinking caret, asks for one frame then: `redraw_after(delay)` or `redraw_at(time)` on the frame clock. The loop sleeps until that time, draws the widget once, and draws nothing in between, so a caret that blinks twice a second costs two frames a second. A widget that is removed or hidden before its time comes is not drawn.

On Windows, frames keep coming while a window is being moved or resized, inside the system's own move-and-size loop.
