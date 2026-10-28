# Memory and safety

## The problem, precisely

A memory leak rarely crashes a program by itself; it makes the program grow until the system starts swapping or ends it. Crashes come from a different family: use after free, out-of-bounds reads, data races, unhandled exceptions. In GUI code the classic form is a callback that captured a widget by reference and runs after the widget is gone. smalti addresses both families.

## By construction

- **`Ref<T>` instead of references.** `add<T>()` returns a `Ref<T>`: a slot index and a generation number, 8 bytes, copyable, safe to capture by value anywhere. Using a `Ref` whose widget was removed is a contract violation that never touches freed memory; `if (ref)` tests for it first. A slot is retired before its generation number could wrap, so an old `Ref` can never come back to life. A `Ref` owns nothing, so captures can never form a cycle; cycles of `shared_ptr` in callbacks are the usual source of GUI leaks.
- **Clear ownership.** The app owns windows, windows own their trees, parents own children.
- **Deferred removal.** `remove()` and `close()` take effect once the outermost event is handled, also when a callback runs a nested event loop.
- **Callbacks outlive their own replacement.** A handler that installs a new handler for the same event keeps running safely to its end.
- **One UI thread, enforced** on every `Ref` access (one comparison, so it stays in release builds).
- **Exceptions never cross the operating system.**
- **Budgets.** The glyph cache has a byte limit (4 MB by default) with least-recently-used eviction; `app.memory_stats()` reports frame buffers, caches and GPU textures.
- **Untrusted bytes in one place in v0.1:** font files, through a bounds-checked reader. Every read is checked against the file's length, and one glyph's outline is limited in how many parts and points it may expand to, so a small hostile font cannot multiply itself into gigabytes. Tested with damaged copies of Inter and with fonts built by hand to be hostile.
- **No allocation per frame.** Once a screen has been drawn once, animating it allocates nothing, as long as its text fits in the glyph cache's budget (tested). A screen showing more distinct glyphs than the budget holds evicts and re-renders some every frame, and those allocate.

## Contract violations

A contract violation (a dead `Ref`, a call from another thread, `add<T>()` on a widget that takes no children) calls the contract handler. The default prints the message and aborts; `set_contract_handler()` replaces it. Tests install one that throws.

## What this does not promise

Your own code can still do anything C++ allows: a lambda that captures your own object by reference is yours to get right. The guarantee is that smalti's API gives you no way to dangle, and that its internals are checked by sanitizers, churn tests and injected bugs. It is not a proof.
