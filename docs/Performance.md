# Performance

What smalti costs, measured on one Windows 11 desktop with an NVIDIA graphics card and a 165 Hz display, Release builds from MSVC 2022, unless a row says otherwise. These are single-machine measurements, not guarantees; the method is given so they can be repeated.

## Memory and idle cost

| | Private memory | Working set | CPU when idle |
|---|---|---|---|
| `hello`, GPU path (Direct3D 11) | 55 MB | 50 MB | 0.0% over 5 s |
| a 1440x900 window, CPU path only (no GPU device) | 14 MB | 28 MB | 0.0% over 3 s |
| Electron 44.7.1, a hello-world page in a 1440x900 window (4 processes) | 149 MB | 302 MB | about 1% |

Most of the GPU path's memory is the graphics driver's: creating a Direct3D 11 device costs about 50 MB of private memory on this machine before anything is drawn; smalti's own share is a few megabytes plus its frame buffers (width x height x 4 bytes each). `AppOptions{.presentation = Presentation::cpu}` never creates the device, for the smallest footprint.

Measured with the process's private bytes and working set (Windows task manager counters) after the window settled; Electron's figures are the sum over its processes.

## Startup

- smalti's own code, from `main()` to the first frame on screen (CPU path): 80 to 125 ms.
- Creating the Direct3D 11 device: 260 ms or more on this driver. smalti does it on a one-shot background thread, so the first frame never waits for it.
- Electron's window appeared after about 1.8 s, measured the same way.

## Drawing

A full redraw (what a resize costs) of a busy reference screen with shadows, gradients and text:

| Size | Dark theme | Black theme (no shadows) |
|---|---|---|
| 1920x1080 | 14 ms | 10 ms |
| 3440x1440 | 26 ms | 17 ms |
| 3840x2160 | 28 ms | 18 ms |

Normal frames redraw only what changed: a hover redraws one button, a fraction of a millisecond.

- **Soft shadows:** computing the full integral per pixel took about 100 ms per full redraw. The closed-form edges and cached corner tiles brought it down 7 to 8 times; the result matches the direct computation to within one step in 255 (tested).
- **No allocation per frame:** once a screen has been drawn, animating it performs no heap allocation (tested by counting `operator new` calls).

## Animation

- A continuously animating indeterminate progress bar at 165 frames per second costs about 6% of one core in the gallery.
- A scrolling prototype at 165 Hz and 3440 pixels wide had a median frame interval of exactly one refresh, and the display repeated a frame on 0 to 4% of refreshes across runs on a busy machine (counted with DXGI's frame statistics). Scrolling as a widget arrives in v0.3.
