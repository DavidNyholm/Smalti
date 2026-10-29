# Themes

A theme is a text file in JSON with comments. It sets the colours, sizes, fonts and motion that every built-in widget draws with. The three built-in themes are such files ([`themes/dark.jsonc`](../themes/dark.jsonc), `black.jsonc` and `light.jsonc`), compiled into the library. This page describes the whole format, so a person or a language model can write a valid theme from it alone.

> **In `beta`, for 0.2.** Theme files, widget styles included, can be read, checked and applied. Still to come in 0.2: live reload, which restyles a running app when the file is saved.

## A theme in a minute

```jsonc
// night.jsonc: Dark, with a teal accent and rounder corners.
{
  "name": "Night",
  "base": "dark",  // start from a built-in theme and list only what changes
  "tokens": {
    "accent": "#2bb5a6",
    "accent.hover": "#3cc4b5",
    "radius.control": 10,
    "radius.card": 16,
  },
}
```

`accent.soft` changes with the accent without being listed, because Dark defines it as `"accent 18%"`.

Check the file, see it, and use it:

```bash
smalti-theme check night.jsonc
```

```bash
gallery night.jsonc
```

```cpp
sm::ThemeLoad r = sm::load_theme("night.jsonc");  // or sm::parse_theme(text, "night.jsonc")
if (r.ok()) {
  app.set_theme(r.theme);
} else {
  for (const sm::ThemeError& e : r.errors)
    std::cerr << e.line << ':' << e.column << ": " << e.message << '\n';
}
```

`smalti-theme` is built with the library (the `SMALTI_BUILD_TOOLS` option, on when smalti is the top-level project). The gallery lists the file's theme after the built-in ones.

## The file

- JSON, plus `//` and `/* */` comments and trailing commas: what VS Code calls jsonc. UTF-8; a byte-order mark at the start is skipped.
- The top level is an object with up to four keys, all optional:

| Key | Value |
|---|---|
| `name` | The theme's name, a string. Without it, the file's name: `night.jsonc` is called "night". |
| `base` | The built-in theme the file starts from: `"dark"` (the default), `"black"` or `"light"`. The file then lists only what it changes. `"none"` starts from nothing, so the file must set every token, as the built-in files do. |
| `tokens` | An object of token names and values; the tokens are listed below. |
| `widgets` | Changes to single widgets, over the look the tokens give them: [Widget styles](#widget-styles) below. |

- Any other key is an error, and so is a key set twice.

## Values

| Kind | Written as | Examples |
|---|---|---|
| Colour | `"#rrggbb"` or `"#rrggbbaa"`: sRGB in hexadecimal, either case | `"#5b8cf0"`, `"#00000080"` |
| | the name of another colour token | `"accent"` |
| | either of those, a space, and an opacity from 0% to 100%, which multiplies the colour's own opacity | `"accent 18%"`, `"#ffffff 7%"` |
| Size | a number, in pixels at 100% scale; the window scales it | `8`, `13.5` |
| Duration | whole milliseconds, as a string | `"120ms"` |
| Switch | `true` or `false` | `true` |
| Text blend | `"adaptive"`, `"srgb"` or `"linear"` | `"adaptive"` |

Instead of a value, any token can name another token of the same kind: `"radius.card": "radius.control"`. Same kind means a colour names a colour, a radius a radius, a font size a font size, and so on, as the Kind column below gives them. A reference reads the other token in the finished theme, after the file's changes, so a file that changes `accent` also changes every token defined from it. References can be chained; tokens that refer to each other in a loop are an error.

## Tokens

The last column is Dark's value; [`themes/black.jsonc`](../themes/black.jsonc) and [`themes/light.jsonc`](../themes/light.jsonc) have the others.

| Token | Kind | Range | What it styles | Dark |
|---|---|---|---|---|
| `surface.titlebar` | colour | | a title bar; no built-in widget draws one yet | `#121316` |
| `surface.sidebar` | colour | | sidebars, such as the gallery's | `#16171a` |
| `surface.background` | colour | | the window's background | `#1b1c20` |
| `surface.raised` | colour | | cards, secondary buttons, check boxes, radio buttons | `#222328` |
| `line.border` | colour | | outlines of cards, secondary buttons, switches that are off, and the slider's knob | `#ffffff 7%` |
| `line.divider` | colour | | separators | `#ffffff 6%` |
| `state.hover` | colour | | the tint under the pointer on secondary and ghost buttons | `#ffffff 4%` |
| `state.selected` | colour | | slider and progress tracks, a switch that is off | `#ffffff 8%` |
| `text.primary` | colour | | text | `#e7e7ea` |
| `text.secondary` | colour | | secondary labels, the knob of a switch that is off | `#a3a4ad` |
| `text.muted` | colour | | captions, the edges of unchecked boxes and rings | `#6e7079` |
| `accent` | colour | | primary buttons, the text of ghost buttons, checked boxes, switches and radio buttons, focus rings | `#5b8cf0` |
| `accent.hover` | colour | | primary buttons under the pointer | `#6e9af2` |
| `accent.on` | colour | | text and marks drawn on the accent | `#ffffff` |
| `accent.soft` | colour | | a faint accent for your own widgets | `accent 18%` |
| `status.success` | colour | | your own widgets | `#3dbb7e` |
| `status.warning` | colour | | your own widgets | `#e0a23b` |
| `status.danger` | colour | | danger buttons | `#e5544b` |
| `depth.shadows` | switch | | soft shadows under cards and the slider's knob; with `false`, cards get a top highlight instead (the Black theme) | `true` |
| `depth.shadow` | colour | | the shadows | `#000000 35%` |
| `depth.highlight` | colour | | the top edge of cards when shadows are off | `#ffffff 0%` |
| `radius.control` | radius | 0 to 100 | corners of buttons and their focus rings | `8` |
| `radius.card` | radius | 0 to 100 | corners of cards | `12` |
| `size.control` | control size | 16 to 96 | the height of buttons | `34` |
| `font.caption` | font size | 6 to 96 | captions | `12` |
| `font.body` | font size | 6 to 96 | body text and controls | `14` |
| `font.title` | font size | 6 to 96 | titles | `18` |
| `font.heading` | font size | 6 to 96 | headings | `24` |
| `motion.fast` | duration | 0 to 5000 ms | hover and press, check boxes, radio buttons | `"120ms"` |
| `motion.base` | duration | 0 to 5000 ms | switches, a progress bar's fill, a button let go after a keyboard press | `"200ms"` |
| `text.blend` | text blend | | how glyph edges blend; `adaptive` keeps light-on-dark and dark-on-light text at a similar weight ([Text and fonts](Text-and-fonts.md)) | `"adaptive"` |
| `text.contrast` | contrast | -0.5 to 2 | sharpens glyph edges above 0, softens them below | `0.1` |
| `text.snap` | switch | | stretches text slightly so the x-height lands on a whole pixel | `true` |
| `dark` | switch | | asks the platform for a dark window frame | `true` |

## Widget styles

Every built-in widget draws a look made from the tokens. The `widgets` section changes single widgets on top of that, so it lists only what differs:

```jsonc
"widgets": {
  "Button": { "radius": 6 },
  "Button.primary": {
    "fill": "#2bb5a6",
    "hover": { "fill": "#3cc4b5" },
    "pressed": { "fill": "#239a8d" },
  },
  "CheckBox": { "checked": { "fill": "status.success" } },
  "Label.caption": { "text": "text.secondary" },
  "Switch": { "disabled": { "opacity": "30%" } },
}
```

- **Keys** name a widget, or a widget and one of its variants: `"Button"` styles every button, `"Button.primary"` primary buttons only. A variant's setting wins over the widget's, in whichever order they are written.
- **Properties** at the top of a widget's style are its colours at rest, and `radius` for the widgets that have one. Their values are written as token values are: a colour as `#rrggbb`, `#rrggbbaa`, a colour token, or either with an opacity; `radius` as a number or a radius token.
- **States** are blocks beside the properties: `hover`, `pressed`, `checked`, `focused` and `disabled`, each setting colours for that state. A state's colour fades in with the widget's own motion (under the pointer, over `motion.fast`). They apply in that order, so where `hover` and `checked` both set a colour, a checked box under the pointer shows the checked one. `focused` means keyboard focus, while the focus ring shows, and applies at once.
- **Disabled** can set colours, which are then drawn as they are, and `opacity`, a percentage that a disabled control's colours are multiplied by: `"45%"` unless set.
- **A state you do not set keeps its built-in look.** Most built-in states are tokens (a primary button's hover is `accent.hover`, so a file that changes its fill should set its hover fill too). Two are relative: under the pointer, secondary and ghost buttons lay `state.hover` (half again as strong) over their fill, whatever the fill is; and a pressed button of any variant darkens its hover fill with 12% black.

| Widget | Variants | Colours | Radius | States |
|---|---|---|---|---|
| `Button` | `primary`, `secondary`, `ghost`, `danger` | `fill`, `border`, `text`, `focus` | yes | hover, pressed, focused, disabled |
| `CheckBox` | | `fill` (the box), `border`, `mark` (the tick), `text`, `focus` | yes | hover, checked, focused, disabled |
| `Switch` | | `track`, `border`, `knob`, `text`, `focus` | | hover, checked (on), focused, disabled |
| `RadioButton` | | `fill`, `border` (the ring), `mark` (the dot), `text`, `focus` | | hover, checked (selected), focused, disabled |
| `Slider` | | `track`, `fill` (the track up to the knob), `knob`, `border` (the knob's), `focus` | | hover, focused, disabled |
| `ProgressBar` | | `track`, `fill` | | disabled |
| `Separator` | | `fill` | | |
| `Label` | `body`, `secondary`, `caption`, `title`, `heading` | `text` | | disabled |
| `Card` | | `fill`, `border` | yes | |

`focus` is the colour of the focus ring. The built-in look, which a style changes:

- **Button:** radius `radius.control`. Primary: fill `accent`, under the pointer `accent.hover`; text `accent.on`. Secondary: fill `surface.raised`, border `line.border`, text `text.primary`. Ghost: no fill, text `accent`. Danger: fill `status.danger` at 14%, 20% under the pointer; text `status.danger`.
- **CheckBox:** radius 5. Fill `surface.raised`, checked `accent`. Border `text.muted`, under the pointer `text.secondary`, gone when checked. Mark `accent.on`.
- **Switch:** track `state.selected`, on `accent`. Border `line.border`, gone when on. Knob `text.secondary`, halfway to `text.primary` under the pointer, `accent.on` when on.
- **RadioButton:** fill `surface.raised`. Border `text.muted`, under the pointer `text.secondary`, selected `accent`. Mark `accent`.
- **Slider:** track `state.selected`, fill `accent`, knob `#ffffff`, border `line.border`.
- **ProgressBar:** track `state.selected`, fill `accent`.
- **Separator:** fill `line.divider`.
- **Label:** `text.primary` for body, title and heading; `text.secondary` for secondary; `text.muted` for caption. The sizes are the `font` tokens.
- **Card:** fill `surface.raised`, border `line.border`, radius `radius.card`; its shadow or top highlight follow the `depth` tokens.
- **Every control:** text `text.primary` where not given above, focus `accent`, disabled at 45%.

In code, the section's rules are `Theme::widget_styles`, a list of `StyleRule` (widget, variant, state, property, value), which a program can also build itself. A rule that names no real widget, variant, state or property is a contract violation when the theme is set.

## Errors

A theme is never half-applied. `load_theme` and `parse_theme` build the whole theme (the base, then the file's changes) before anything uses it. A file with any error gives the base theme unchanged in `ThemeLoad::theme` (Dark if the file does not get as far as naming one), and `ThemeLoad::errors` lists every problem found, in file order:

```
night.jsonc:4:11: "base" is "dark", "black", "light" or "none"; got "drak"; did you mean "dark"?
night.jsonc:6:5: unknown token "accnet"; did you mean "accent"?
night.jsonc:7:23: "radius.control" takes a radius from 0 to 100, or the name of another such token; got 200
```

- Line and column are 1-based, and the column counts characters, as editors do. A file that cannot be read at all (missing, a folder, too large) is one error at line 0.
- A syntax error stops reading at that point, so it is reported alone. Otherwise every unknown key, wrong value and loop is reported, up to 100, followed by a count of the rest. In the `widgets` section, a name that is not a widget, variant, state or property is reported with the names that would have been valid there.
- Limits, so any file costs little to read: 1 MB per file, objects and arrays nested at most 32 deep, 64 KB per string, 65536 values.
- A theme is data only: it cannot run code, and reading one touches nothing but that file. That is what makes it safe to load a theme from anywhere, a user's included.
- `smalti-theme check` prints the same errors as `file:line:column: message` and exits with 1 when any file has one, 2 when the command line is wrong.

## Writing a theme with a language model

This page is the complete description of the format. Give a model this page, the raw Markdown of `docs/Themes.md` in the repository, and the colours you want; then run `smalti-theme check` on what it writes and hand back the errors, which name the line, the column and usually the fix.
