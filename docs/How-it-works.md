# How it works

## The layers

```
 your app
   |
 widgets      Label, Button, CheckBox, ... (src/widgets: platform-free)
   |
 core         widget tree, Ref registry, layout, input, focus, motion, damage (src/core)
   |
 render       canvas, rasterizer, TrueType reader, glyph cache (src/render)
   |
 platform     the porting interface (src/platform/platform.hpp)
                win32 (+ Direct3D 11)     x11     headless
```

Everything above `platform` is the same code on every system.

## Self-drawn and retained

smalti draws every widget itself rather than wrapping native controls: one implementation per widget, the same behaviour everywhere, and a new platform needs only a window and pixels. The widgets form a tree that lives between frames (retained, not immediate mode). The tree is what makes motion possible (each widget keeps its animation state), lets only what changed be redrawn, keeps idle CPU at zero, and is what a screen reader will read in v0.5.

## A frame

1. Input arrives from the platform. The core hit-tests the tree, routes the event (with mouse capture between press and release, and keyboard events to the focused widget, bubbling up to its ancestors), and the widgets change state.
2. A widget that changed calls `invalidate()`, which records its rectangle (plus how far its paint may reach, for shadows and focus rings) as damaged and asks the platform for a frame.
3. At the next moment the display can take a frame, the core lays out the tree if anything changed size, then repaints only the damaged rectangles, each clipped to itself, into the frame buffer.
4. The platform puts those rectangles on screen: GDI or XPutImage on the CPU path; on Windows' GPU path, an upload of the damaged rectangles into a texture, a copy into a flip-model swap chain, and a present at the next refresh.
5. If something is still animating, another frame is requested; otherwise nothing more happens until the next input.

## Drawing

The canvas draws into 32-bit pixels with one rasterizer for every shape and glyph: exact-area coverage from signed-area accumulation, curves flattened to within a twentieth of a pixel. Soft shadows are computed in closed form: the straight edges exactly, the corners by Evan Wallace's method, cached per blur and radius so a card's shadow costs a table lookup per pixel. Gradients and shadows are dithered so dark ramps show no steps on high-contrast screens.

## Presentation on Windows

The first frames go through GDI, because creating a Direct3D device takes hundreds of milliseconds on some drivers. A one-shot thread creates the device in the meantime; when it is ready, each window moves to a flip-model swap chain whose frame-latency waitable object paces frames to the display. If the device is lost, the window falls back to GDI.
