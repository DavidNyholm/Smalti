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

`app.run()` sleeps in the operating system (`MsgWaitForMultipleObjectsEx` on Windows, `poll` on Linux) until input, a requested frame, or posted work arrives. There is no polling and no frame loop: a window that is not animating draws nothing and uses no CPU. `run()` returns when the last window closes or `app.quit()` is called.

## Callbacks

Each event has one callback, set with `on_click`, `on_change`, `on_select`. Callbacks are move-only (`smalti::Callback`), so they may own what they capture.

- **A callback may remove its own widget, its parent, or close its window.** Removal happens once the event has been handled, so no code ever runs inside a destroyed object.
- **An exception thrown by a callback** stops the loop and is rethrown from `app.run()`. It never unwinds through the operating system.

## Frames and motion

Widgets mark what changed (`invalidate()`); the window redraws only those rectangles at its next frame, in step with the display. Animations are values on the frame clock (`animate(motion, target, duration)`, read with `at(motion)`), so they cost nothing between animations and show the right value at any frame rate. With the system's animation setting turned off, they jump straight to the end.

On Windows, frames keep coming while a window is being moved or resized, inside the system's own move-and-size loop.
