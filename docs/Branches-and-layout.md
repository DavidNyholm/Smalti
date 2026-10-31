# Branches and layout

`main` holds verified states only: every commit on it has passed the full suite with MSVC 2022 and GCC 14, the sanitizers, the injected-bug script, and a review round. Work happens on `beta`, which is merged into `main` when a version is done and checked; each version is a tag on `main`. To get something known to work, build `main` or a tag; to get the latest, build `beta`.

## Layout

```
include/smalti/smalti.hpp      include this; pulls in the rest
include/smalti/app.hpp         App, AppOptions, MemoryStats
include/smalti/window.hpp      Window, WindowOptions
include/smalti/widget.hpp      Widget, Motion, Align
include/smalti/layout.hpp      Column, Row, Spacer, Grid, Card
include/smalti/widgets.hpp     Label, Button, CheckBox, Switch, RadioButton, Slider, ProgressBar, Separator,
                               TextBox
include/smalti/painter.hpp     Painter, TextStyle: what custom widgets draw with
include/smalti/theme.hpp       Theme, the built-in Dark, Black and Light, and reading theme files
include/smalti/ref.hpp         Ref<T>, WidgetId, the contract handler
include/smalti/input.hpp       mouse and key events, Key, Cursor
include/smalti/testing.hpp     testing::Driver: simulated input against the headless backend
src/core/                      the tree, Ref registry, window pipeline, layout, painter
src/widgets/                   the built-in widgets and their shared style helpers
src/render/                    canvas, rasterizer, TrueType reader, text and glyph cache
src/platform/                  the porting interface and the win32, x11 and headless backends
src/theme/                     the theme file reader (JSON with comments), the tokens, the built-in themes
themes/                        the built-in themes, as theme files
fonts/                         Inter (OFL) and where it came from
cmake/embed_files.cmake        compiles the fonts and the built-in themes into the library
examples/                      hello, gallery
tests/                         the suite (check.hpp harness, one file per area)
tools/smalti_theme.cpp         smalti-theme, which checks theme files
tools/privacy_check.py         commit and push hooks that refuse local paths and secrets
tools/mutate.py                the suite against deliberate bugs
tools/publish_wiki.py          copies docs/ to the GitHub wiki
docs/                          these pages
```
