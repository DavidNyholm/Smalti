# smalti

A C++20 GUI library for desktop applications that look like current apps without shipping a browser. It draws every widget itself, animates every state change, and uses no CPU when nothing moves. Windows and Linux, MIT licensed, no third-party dependencies.

*Smalti* are the glass tiles of Byzantine mosaics, each set at a slight angle so the wall catches the light.

**Status:** 0.1.1 is the latest release: 0.1.0 "First light" (the look, motion and basic controls) with the fixes of two more review rounds. It is verified locally on Windows 11 (MSVC 2022 17.14) and Ubuntu 24.04 under WSL (GCC 14). The API will change during 0.x. Text input arrives in v0.2. Screen readers are not supported before v0.5, so smalti cannot yet be recommended for software that must be accessible. The [wiki](https://github.com/DavidNyholm/Smalti/wiki) (also in [`docs/`](docs/Home.md)) has everything beyond this page.

## Quick start

Needs CMake 3.21 or newer and a C++20 compiler: MSVC 2022, GCC 13+ or Clang 16+. On Linux, the X11 headers (`libx11-dev`).

```bash
cmake --preset windows
cmake --build --preset windows
ctest --preset windows
build\windows\Debug\gallery.exe
```

On Linux use the `linux` preset and run `build/linux/gallery`. The `headless` preset builds without any window system, for tests and rendering to memory.

In your own project, pinned to a commit hash (tags can be moved; a hash cannot):

```cmake
include(FetchContent)
FetchContent_Declare(smalti GIT_REPOSITORY https://github.com/DavidNyholm/Smalti.git GIT_TAG <full commit hash>)
FetchContent_MakeAvailable(smalti)
target_link_libraries(app PRIVATE smalti::smalti)
```

## What it looks like

```cpp
#include <smalti/smalti.hpp>
namespace sm = smalti;

int main() {
  sm::App app;
  auto win = app.open_window({"Hello, smalti", {380, 220}});
  auto col = win->add<sm::Column>(sm::StackOptions{.spacing = 12, .padding = sm::Insets::all(28)});
  col->add<sm::Label>("Hello, smalti", sm::LabelOptions{.role = sm::TextRole::heading});
  auto status = col->add<sm::Label>("Nothing clicked yet.", sm::LabelOptions{.role = sm::TextRole::secondary});
  col->add<sm::Button>("Click me", sm::ButtonVariant::primary)->on_click([status] {
    status->set_text("Clicked.");
  });
  return app.run();
}
```

Every widget is reached through a `Ref<T>`: a checked handle you can copy into any callback. If the widget is removed, using the `Ref` is a reported error, never a read of freed memory. More in [What it looks like](docs/What-it-looks-like.md), with screenshots of the gallery in the three built-in themes.

## What it does

- **A modern look by default:** layered surfaces, hairline borders, soft shadows computed in closed form, and Inter embedded as the default font. Three built-in themes: Dark, Light, and Black for OLED screens (true black, with depth from borders because shadows cannot be seen on black). In `beta`, a theme is also a file you can write and check yourself: [Themes](docs/Themes.md).
- **Motion everywhere:** hover, press, focus and value changes animate. Each animation is a function of the frame clock, so it costs nothing between animations and is exactly where it should be at any frame rate.
- **Smooth and light:** widgets are drawn on the CPU and composited on the GPU (Direct3D 11 on Windows), presented in step with the display. Only what changed is redrawn, and an idle window uses no CPU. The first frame never waits for the GPU driver.
- **Safe to hold:** `Ref<T>` handles, deferred removal (a callback may remove its own widget or close its own window), one UI thread enforced on every call (except `App::post`, the one call made for other threads), exceptions from callbacks rethrown from `run()` rather than through the OS.
- **Keyboard first-class:** Tab order, a focus ring shown only to keyboard users, Space and Enter, arrows in sliders and radio groups.
- **v0.1 widgets:** Label, Button (primary, secondary, ghost, danger), CheckBox, Switch, RadioButton, Slider, ProgressBar (with an indeterminate mode), Separator, Card; layout with Column, Row, Grid and Spacer.
- **No dependencies:** the operating system and its SDK, the compiler, CMake. Nothing is downloaded at build time. Direct3D and the other Windows DLLs outside the protected KnownDLLs list are loaded from the system directory only. See [Dependencies and supply chain](docs/Dependencies-and-supply-chain.md).

## Read more

- [Build and use](docs/Build-and-use.md): presets, FetchContent, `find_package`, the examples
- [Widgets](docs/Widgets.md) and [Layout](docs/Layout.md)
- [Events and threading](docs/Events-and-threading.md) and [Memory and safety](docs/Memory-and-safety.md)
- [How it works](docs/How-it-works.md) and [Performance](docs/Performance.md)
- [How it is checked](docs/How-it-is-checked.md): the tests, the sanitizers, the injected bugs, the review rounds
- [Porting to a new platform](docs/Porting-to-a-new-platform.md), [Forking and contributing](docs/Forking-and-contributing.md)
- [Branches and layout](docs/Branches-and-layout.md) and the [Roadmap](docs/Roadmap.md)

## Built with

Written by Claude (**Opus 5.5**, model id `claude-opus-5-5`) in Claude Code. Reviewed by **GPT-6-Astra** (model id `gpt-6-astra`) through the Codex CLI, as a second opinion whose findings Claude verified and fixed. Directed and tested by David. Commits carry a `Co-Authored-By` trailer naming the model that wrote them.

## License

The code is MIT licensed: see [LICENSE](LICENSE). The embedded Inter font is under the SIL Open Font License 1.1: see [fonts/OFL.txt](fonts/OFL.txt) and [fonts/SOURCE.md](fonts/SOURCE.md).
