import { invoke } from "@tauri-apps/api/core";

export const viewportProtocolVersion = "kb.editor.viewport/v1" as const;

export function hasNativeEditorHost(): boolean {
  return typeof window !== "undefined" && "__TAURI_INTERNALS__" in window;
}

function nativeInvoke<T>(command: string, args?: Record<string, unknown>): Promise<T> {
  if (!hasNativeEditorHost()) {
    throw new Error("Native editor windows are unavailable in the browser preview. Launch the desktop editor host to use them.");
  }
  return invoke<T>(command, args);
}

export type ViewportLifecycleKind =
  | "created"
  | "resized"
  | "suspended"
  | "resumed"
  | "lost"
  | "destroyed";

export interface ViewportSurfaceEvent {
  protocolVersion: typeof viewportProtocolVersion;
  generation: number;
  sequence: number;
  kind: ViewportLifecycleKind;
  physicalSize: {
    width: number;
    height: number;
  };
  scaleFactorMicros: number;
}

export interface ViewportOpenReply {
  protocolVersion: typeof viewportProtocolVersion;
  disposition: "created" | "focused";
  surface: ViewportSurfaceEvent;
}

function isViewportOpenReply(value: unknown): value is ViewportOpenReply {
  if (typeof value !== "object" || value === null) {
    return false;
  }

  const reply = value as Partial<ViewportOpenReply>;
  const surface = reply.surface;
  return (
    reply.protocolVersion === viewportProtocolVersion &&
    (reply.disposition === "created" || reply.disposition === "focused") &&
    typeof surface === "object" &&
    surface !== null &&
    surface.protocolVersion === viewportProtocolVersion &&
    typeof surface.generation === "number" &&
    typeof surface.sequence === "number" &&
    typeof surface.kind === "string" &&
    typeof surface.physicalSize?.width === "number" &&
    typeof surface.physicalSize?.height === "number" &&
    typeof surface.scaleFactorMicros === "number"
  );
}

export async function openViewportScreen(): Promise<ViewportOpenReply> {
  const reply = await nativeInvoke<unknown>("viewport_open_screen");
  if (!isViewportOpenReply(reply)) {
    throw new Error("The viewport host returned an incompatible IPC response.");
  }
  return reply;
}

export type DetachablePanelId = "project-files" | "scene-objects" | "details" | "console";

interface DetachedPanelWindowReply {
  panelId: string;
  windowLabel: string;
  disposition: "created" | "focused";
}

const hostPanelIds: Readonly<Record<DetachablePanelId, string>> = {
  "project-files": "projectFiles",
  "scene-objects": "sceneObjects",
  details: "details",
  console: "console",
};

export async function openDetachedPanelWindow(panelId: DetachablePanelId): Promise<DetachedPanelWindowReply> {
  const reply = await nativeInvoke<unknown>("editor_open_panel_window", { panelId: hostPanelIds[panelId] });
  if (
    typeof reply !== "object" || reply === null ||
    typeof (reply as Partial<DetachedPanelWindowReply>).panelId !== "string" ||
    typeof (reply as Partial<DetachedPanelWindowReply>).windowLabel !== "string" ||
    !["created", "focused"].includes((reply as Partial<DetachedPanelWindowReply>).disposition ?? "")
  ) {
    throw new Error("The detached panel host returned an incompatible IPC response.");
  }
  return reply as DetachedPanelWindowReply;
}
