# UI safe areas

The Windows player uses per-monitor DPI awareness in both windowed and fullscreen
mode. Windowed DPI changes apply the operating system's suggested bounds, then
resize the render surface and UI through the normal client-size notification.
Anchors, canvas scaling and pointer hit tests share the resulting client-pixel
coordinates. `kb_game_window_dpi` checks upward and downward DPI notifications,
orientation changes and control hit testing in an invisible native window; it
does not change physical display settings. The packaged UI visual test checks
rendered pixels after live client-size changes.

Screen-space canvases respect the platform safe area by default. `UICanvas::respectSafeArea`
can disable this for backgrounds that intentionally extend behind system UI. Insets are
nonnegative viewport pixels in left, top, right, bottom order. The scene layout clamps
oversized insets to an empty usable area; world-space canvases are unaffected.

The browser player measures CSS `env(safe-area-inset-*)`, the visual viewport and
docked virtual-keyboard overlays before each scene tick. It intersects these bounds
with the canvas rectangle, then converts CSS pixels to backbuffer pixels. An embedded
canvas already outside a cutout does not receive that inset twice. The supplied shell
uses `viewport-fit=cover`. A floating keyboard is not represented by rectangular insets.

The Android window uses edge-to-edge rendering and requests resize-compatible
IME inset reporting, including on older Android versions. The edge-to-edge surface
remains full-size while its safe area excludes the keyboard. Before layout,
the native host reads GameActivity's system-bar, display-cutout, mandatory-gesture,
IME and waterfall insets, taking the maximum on each edge. This avoids adding
overlapping system regions. Insets update after orientation and surface changes.

Viewport or clipping-area changes reveal a focused text field inside its scroll view,
preserving focus and committed text. Ordinary scrolling does not force focus back
into view. The editor's device preview and headless runner use the same scene UI API.

```sh
kb_cli run --project Sample --frames 3 --viewport 800x600 --safe-area 100,50,30,20
```

`--viewport` uses positive integer pixel dimensions and overrides viewport dimensions
from input replay. `--safe-area` accepts four finite nonnegative values. Both options
are also available through MCP `run_scene` as `viewport` and `safeArea`. Replay input
can exercise native UI hit testing at these dimensions; it does not emulate an OS keyboard.

The native UI tests cover portrait/landscape, cutouts, gesture bars, keyboard-sized
insets, collapsed bounds and focused-field clipping changes. `kb_cli_tests safe-area`
authors corner controls through the normal commands and checks their runtime hit tests.
`run_game_safe_area_tests.py` cooks a text-entry scene and checks hardware-rendered
browser output, cutouts, an overlay keyboard, embedded canvases and portrait DPI changes.
The Android integration APK compares the production native insets with actual window
insets in both orientations and after surface recreation; its transparent test window
does not exercise a real IME or the full packaged renderer. Its separate `--ime-only`
mode opens a real keyboard and records surface, decor, visible-frame and IME geometry.
