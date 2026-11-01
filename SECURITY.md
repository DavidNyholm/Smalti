# Security

**Maintenance:** a personal project, maintained when time allows, with no service-level promise. This line changes if the repository is archived.

**Reporting a problem:** use GitHub's private vulnerability reporting on this repository (Security, then "Report a vulnerability"). Please do not open a public issue for a security problem.

## What smalti promises, and what it does not

- **Untrusted input** reaches smalti in three places in v0.2. Font files, read by the TrueType reader: every read goes through a bounds-checked reader, damaged fonts are refused or rendered without their damaged parts, and the reader is tested against truncated and corrupted copies under AddressSanitizer. Theme files: at most 1 MB, nested 32 deep, with limits on strings and values, tested against thousands of damaged copies. And the clipboard: text from other programs is made valid UTF-8 and read only up to 16 MB, every wait for another program is bounded, and on X11 the App answers other programs within limits on how many transfers run at once and how long they wait (checked against misbehaving programs written for the purpose). Images, the next reader of untrusted bytes, arrive in v0.3 under the same rules.
- **No code is ever loaded from data.** Fonts (and later themes and images) are data. There are no plugins.
- **No third-party code** is in the build. On Windows, Direct3D, DXGI and DWM are loaded at run time from the system directory only (`LOAD_LIBRARY_SEARCH_SYSTEM32`), so a DLL planted next to an application is not picked up. Applications can protect their own imports the same way with the linker option `/DEPENDENTLOADFLAG:0x800`.
- **Memory safety** of the library's API is by construction (checked `Ref` handles, deferred removal, single-thread enforcement) and by testing (sanitizers, churn tests). It is not a proof, and it does not cover the application's own code.
- **Not provided:** sandboxing, privilege separation, or protection against a local attacker who can already run code as the user.
