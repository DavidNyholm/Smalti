# Roadmap

What comes after v0.1, in order. Each version is tagged on `main` only after the full suite, the sanitizers, the injected bugs and a review round pass.

**v0.2 "Words": text input and themes as files.**
- `TextBox`: caret, selection, word jumps, clipboard, undo, password mode, input-method placement.
- Clipboard on both platforms.
- Themes as data files (JSON with comments) with live reload: edit a theme and the running app restyles itself. Precise errors that never half-apply a theme, and a page describing the format for people and for agents. **In `beta`:** the format, the loader, widget styles, live reload, `smalti-theme check` and the [Themes](Themes.md) page; timed redraws for the caret to come; typed text and the input method.
- Grid spans solved together, so spans that overlap across fit columns always get a split that fits when one exists.

**v0.3 "Content": scrolling, lists, images.**
- `ScrollArea` with overlay scrollbars and smooth touchpad scrolling; scrolling by compositor offset, not by redrawing.
- `ListBox` that draws only visible rows (100,000 and more).
- `TextView` for logs.
- A PNG decoder of our own and an `Image` widget; a line icon set.

**v0.4 "Shell": application-shaped UIs.**
- Menus, dropdowns, tooltips, a command palette.
- `Splitter`, `Tabs`, `TreeView`, dialogs.
- A custom title bar with Windows 11 snap layouts, and Mica backdrops.

**v0.5 "Reach": accessibility and tools.**
- UI Automation (Windows) and AT-SPI (Linux), so screen readers can read and operate every widget.
- An inspector, like a browser's developer tools.
- Multi-line editing, drag and drop, file dialogs.

**After v0.5:** a C API and a Python binding; a native Wayland backend; the OpenGL compositor on Linux (v0.1 presents through XPutImage there); compositing through the operating system (DirectComposition); HDR; complex scripts and right-to-left text; continuous integration; an API freeze and v1.0. macOS comes when someone ports it, starting from [Porting to a new platform](Porting-to-a-new-platform.md).
