# Dependencies and supply chain

The goal: nothing a user builds or runs because of smalti can be swapped for something malicious by a third party.

## What smalti uses

- **The operating system and its SDK.**
  - *Windows:* `user32`, `gdi32` and `advapi32` are linked; they are on Windows' protected KnownDLLs list. Direct3D 11 (`d3d11.dll`) and DWM (`dwmapi.dll`) are not on that list, so they are loaded at run time from the system directory only (`LOAD_LIBRARY_SEARCH_SYSTEM32`): a DLL planted next to an application is never picked up.
  - *Linux:* `libX11`, linked.
- **The compiler, its standard library, and CMake.**
- **Nothing else.** No package manager, no FetchContent of dependencies, no submodules, no build step that touches the network.

## What smalti writes instead of importing

The TrueType reader, the rasterizer and the text layout are smalti's own code, so no third-party code is in anyone's build. The trade is honest: our bugs instead of someone else's, but every line in the build is one that was written and reviewed here. Every reader of untrusted bytes goes through one bounds-checked reader type and is tested against damaged input under the sanitizers.

## The one third-party file

Inter, the default font. It is data, not code: it cannot run anything, and only the bounds-checked reader touches it. `fonts/SOURCE.md` records the official release and the SHA-256 of each file.

## For people using smalti

- Pin FetchContent to a **full commit hash**, not a tag: tags can be moved, a hash cannot.
- Protect your own imports on Windows with the linker option `/DEPENDENTLOADFLAG:0x800`.

## If this ever changes

A dependency would be added only if it is copied into the repository (no network at build time), single-purpose, under a permissive licence, pinned by hash with its origin recorded, and tested against damaged input if it reads any.
