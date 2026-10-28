# smalti wiki

smalti is a C++20 GUI library for desktop applications that look like current apps without shipping a browser: self-drawn widgets, motion everywhere, no CPU when idle, Windows and Linux, MIT licensed, no third-party dependencies. The [front page](https://github.com/DavidNyholm/Smalti#readme) has the quick start; these pages have the rest.

- [Build and use](Build-and-use.md): requirements, presets, using smalti in a project, the examples
- [What it looks like](What-it-looks-like.md): the API in a few lines, and screenshots
- [Widgets](Widgets.md): every control in v0.1 and how it behaves
- [Layout](Layout.md): Column, Row, Grid, Spacer and Card
- [Events and threading](Events-and-threading.md): the event loop, callbacks, `post()`, motion
- [Memory and safety](Memory-and-safety.md): `Ref<T>`, removal, what is and is not promised
- [Text and fonts](Text-and-fonts.md): Inter, the TrueType reader, how text is drawn
- [Dependencies and supply chain](Dependencies-and-supply-chain.md): what the library uses and why nothing else
- [How it works](How-it-works.md): the layers, from widgets down to the screen
- [Performance](Performance.md): what it costs, measured
- [How it is checked](How-it-is-checked.md): tests, sanitizers, injected bugs, review rounds
- [Porting to a new platform](Porting-to-a-new-platform.md): the interface a new platform implements
- [Forking and contributing](Forking-and-contributing.md): where things live and how to change them
- [Branches and layout](Branches-and-layout.md): `main`, `beta`, and the file tree
- [Roadmap](Roadmap.md): v0.2 to v0.5 and beyond

These pages live in the repository under `docs/` and are copied to the GitHub wiki by `tools/publish_wiki.py`, so a downloaded or forked copy has them too.
