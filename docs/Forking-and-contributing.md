# Forking and contributing

smalti is meant to be forked. This page says where things are and how to change them; [CONTRIBUTING.md](../CONTRIBUTING.md) says how changes are received here.

## Where things live

See [Branches and layout](Branches-and-layout.md) for the full tree. In short: public headers in `include/smalti/`, widgets in `src/widgets/`, the core in `src/core/`, drawing in `src/render/`, platforms in `src/platform/`, tests in `tests/`.

## Adding a widget

1. Declare it in `include/smalti/widgets.hpp` (or your own header): derive from `Widget`, and override `measure`, `paint`, and the input hooks it needs.
2. Implement it under `src/widgets/` using the shared helpers in `src/widgets/style.hpp`, so it looks like the rest.
3. Add it to `CMakeLists.txt`, to the gallery, and a test in `tests/test_widgets.cpp` that drives it with `testing::Driver`: click it, press its keys, check what it reports.

## Adding a platform

See [Porting to a new platform](Porting-to-a-new-platform.md).

## The checks

```bash
cmake --preset windows && cmake --build --preset windows && ctest --preset windows
python tools/mutate.py
```

On Linux, the `linux-asan` preset runs the suite under AddressSanitizer and UndefinedBehaviorSanitizer. Install the privacy hooks once per clone with `python tools/privacy_check.py --install`.

## Style

One style throughout, set by files in the repository's root, so nobody formats by hand and every change looks like the code around it:

- **C++:** `.clang-format`, Google style at 100 columns. Format the files you change with `clang-format -i <file>`; any clang-format from version 16 on reads the file, and Visual Studio's C++ tools include one.
- **Python** (the scripts in `tools/`): `ruff.toml`, which `ruff format tools` and `ruff check tools` follow.
- **Everything else:** `.editorconfig` (UTF-8, LF line endings, two-space indents, four in Python), which most editors follow by themselves.
- **Names:** `snake_case` functions and files, `PascalCase` types, `trailing_` members, `kPascalCase` constants, `UPPER_CASE` macros (prefixed `SMALTI_`).
- **The rest:** C++20. Comments say why, not what. Zero warnings at `/W4` and `-Wall -Wextra -Wpedantic`. Sources are plain ASCII; anything else is written as an escape.

The commit that brought the whole tree to this style changed nothing but layout: the library, tests and examples compiled to identical object code before and after, with both compilers. It is listed in `.git-blame-ignore-revs`, so GitHub's blame view looks past it, and so does `git blame` after `git config blame.ignoreRevsFile .git-blame-ignore-revs`.
