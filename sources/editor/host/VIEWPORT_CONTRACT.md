# Native Screen viewport contract

## Decision

`Screen` is one webview-free, native Tauri `Window` labelled `kb-editor-screen`.
The Rust host owns the window and its lifecycle; the C++ renderer owns `VkSurfaceKHR`,
the swapchain and all GPU resources. `Game View` is never drawn through HTML or Canvas.

The host uses Tauri's `unstable` native `WindowBuilder`, deliberately rather than
`WebviewWindowBuilder`: a WebView and a Vulkan swapchain must not compete for the same
native drawable. Tauri `Window` exposes `HasWindowHandle` and `HasDisplayHandle`; the
renderer acquires those only through `ViewportHost::with_native_window` and creates the
platform Vulkan surface in-process. Handles never cross JSON IPC.

On Windows this maps to the native Win32 WSI path. On Linux, the renderer must inspect
the obtained raw handles and use Wayland WSI (`VK_KHR_wayland_surface`) when they are
Wayland handles, otherwise its explicit X11 path. It must query presentation support for
the selected GPU and the actual surface. Wayland surface size is compositor-driven, so a
resize/DPI lifecycle event is authoritative and a zero-sized extent suspends rendering.

Embedding this native drawable into a docked WebView is **not** part of this contract.
Tauri's child-window/webview APIs are unstable and a WebView is itself a native renderer;
the portable Windows + Wayland first release therefore uses the separately dockable
`Screen` top-level window. Treat a future in-window native viewport as a separate WSI and
compositor-validation project, not as a CSS overlay.

## IPC v1

The UI invokes `viewport_open_screen` without arguments. It returns:

```ts
type ViewportOpenReply = {
  protocolVersion: "kb.editor.viewport/v1";
  disposition: "created" | "focused";
  surface: ViewportSurfaceEvent;
};

type ViewportSurfaceEvent = {
  protocolVersion: "kb.editor.viewport/v1";
  generation: number;
  sequence: number;
  kind: "created" | "resized" | "suspended" | "resumed" | "lost" | "destroyed";
  physicalSize: { width: number; height: number };
  scaleFactorMicros: number;
};
```

Lifecycle notifications are emitted application-wide as
`kb://viewport/lifecycle/v1`. Consumers reject an unknown `protocolVersion`, ignore an
event with an older `sequence`, and recreate renderer resources after `lost` or a changed
`generation`. The renderer calls `report_surface_lost` after `VK_ERROR_SURFACE_LOST_KHR`,
destroys its old Vulkan objects, then calls `recreate_surface` and uses its new generation;
it does not report that condition through the WebView IPC channel.

`created` gives the renderer permission to acquire native handles. `suspended` stops
rendering/submission; `resumed` or `resized` provides a new non-zero physical extent;
`destroyed` requires destruction of the Vulkan surface and swapchain. A zero width or
height is never rendered.

## Integration

Register the host once in the future Tauri application:

```rust
.manage(kb_editor_host::ViewportHost::new())
.invoke_handler(tauri::generate_handler![
    kb_editor_host::viewport_open_screen,
    kb_editor_host::editor_open_panel_window,
])
```

The renderer bridge must remain in the same process as this host, or be given a dedicated
platform-specific native-handle transfer design. Serialising a window handle to a separate
engine process is invalid and intentionally unsupported here.

## Detached editor panels

`editor_open_panel_window({ panelId })` opens or focuses a WebView window for exactly one
of `projectFiles`, `sceneObjects`, `details`, or `console`. Each has a fixed label
(`kb-editor-panel-*`) so repeated calls focus the existing window instead of creating a
duplicate. `gameView` is rejected: it has no WebView fallback and must use
`viewport_open_screen`. The static, validated route is passed as
`index.html?panel=project-files|scene-objects|details|console`; the UI uses it to render
only the selected detached panel.

## Sources

- [Tauri `WindowBuilder` (native window, marked unstable)](https://docs.rs/tauri/2.11.5/tauri/window/struct.WindowBuilder.html)
- [Tauri `Window` raw display/window handles and lifecycle APIs](https://docs.rs/tauri/2.11.5/tauri/window/struct.Window.html)
- [Tauri native window events](https://docs.rs/tauri/2.11.5/tauri/enum.WindowEvent.html)
- [Tauri's documented Windows synchronous-command deadlock warning](https://docs.rs/tauri/2.11.5/tauri/window/struct.WindowBuilder.html#known-issues)
- [Khronos Vulkan WSI specification: Wayland surface lifetime, sizing and presentation support](https://docs.vulkan.org/spec/latest/chapters/VK_KHR_surface/wsi.html)
