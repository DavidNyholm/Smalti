# Contributing

smalti is a personal project, published so that anyone can use it, learn from it, and fork it.

- **Forks are welcome.** MIT means you may take it in any direction, including closed source, as long as the copyright notice stays.
- **Issues and pull requests are read when there is time.** They may be closed without review, and there is no promise of a reply. The repository may be archived one day; downloads and forks keep working when that happens.
- **If you do send a change:** keep it to one subject, format it with the repository's settings ([Style](docs/Forking-and-contributing.md#style)), run the whole check first (below), and say what you ran. A change that adds a dependency will not be accepted (see [Dependencies and supply chain](docs/Dependencies-and-supply-chain.md)).

## Running the checks

```bash
cmake --preset windows        # or linux, or headless
cmake --build --preset windows
ctest --preset windows
python tools/mutate.py        # the suite against deliberate bugs
```

On Linux, `cmake --preset linux-asan` builds the same suite with AddressSanitizer and UndefinedBehaviorSanitizer.

Before the first push from a clone, install the privacy hooks once:

```bash
python tools/privacy_check.py --install
```

They refuse to commit or push local paths, personal e-mail addresses, secrets and local tool files.

[Forking and contributing](docs/Forking-and-contributing.md) explains where things live, how to add a widget, and how to add a platform.
