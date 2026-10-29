# Build and use

Requirements, presets, the examples, and the ways to put smalti into a project.

## Requirements

- CMake 3.21 or newer.
- A C++20 compiler: MSVC 2022 (17.x), GCC 13 or newer, or Clang 16 or newer.
- Windows 10 or 11, with the Windows SDK that comes with Visual Studio or its Build Tools.
- Linux with an X server (Wayland desktops run smalti through XWayland) and the X11 headers: `sudo apt install libx11-dev` on Debian and Ubuntu.

Nothing is downloaded at build time. The default font and the built-in themes are compiled into the library from `fonts/` and `themes/`.

## Presets

| Preset | What it builds |
|---|---|
| `windows` | Visual Studio 2022, x64, Debug and Release configurations |
| `windows-asan` | the same with AddressSanitizer |
| `windows-insiders` | Visual Studio 2026 (needs CMake 4.2); not a verified configuration |
| `linux`, `linux-release` | Ninja, Debug or Release |
| `linux-asan` | Debug with AddressSanitizer and UndefinedBehaviorSanitizer |
| `headless` | no window system at all: tests and rendering into memory |

```bash
cmake --preset windows
cmake --build --preset windows
ctest --preset windows
build\windows\Debug\gallery.exe
```

On Linux, the same with `linux`, then `build/linux/gallery`.

## Examples

- `hello`: a heading, a label and a button that counts.
- `gallery`: every v0.1 control, the three themes (switch with the radio buttons at the bottom of the sidebar), a second window, and a custom widget (the sidebar). `gallery night.jsonc` opens it in the theme that file describes, listed after the built-in ones, and restyles it each time the file is saved ([Themes](Themes.md)).
- `smalti-theme check <file.jsonc>`: reports every error in a theme file, with its line and column.

## Using smalti in a project

Straight from GitHub, pinned to a full commit hash (tags can be moved; a hash cannot):

```cmake
include(FetchContent)
FetchContent_Declare(smalti GIT_REPOSITORY https://github.com/DavidNyholm/Smalti.git GIT_TAG <full commit hash>)
FetchContent_MakeAvailable(smalti)
target_link_libraries(app PRIVATE smalti::smalti)
```

Or `add_subdirectory` on a copy of the repository, or `cmake --install` and then `find_package(smalti 0.1 REQUIRED)`.

**Windows programs** can start at `main()`: build them with `add_executable(app WIN32 ...)` and the linker option `/ENTRY:mainCRTStartup`, as the examples do, so no console window opens.

**Options:** `SMALTI_BUILD_EXAMPLES`, `SMALTI_BUILD_TESTS`, `SMALTI_BUILD_TOOLS` (`smalti-theme`) and `SMALTI_INSTALL` default to on only when smalti is the top-level project. `SMALTI_HEADLESS_ONLY` builds without any window system. `SMALTI_SANITIZE` applies the sanitizers to smalti's own targets, never to yours.
