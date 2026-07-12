/** Panel ids are owned by the DockTree; the shell accepts any registered id. */
export type PanelId = string;

export type ToolbarAction = "play" | "pause" | "stop" | "screen";
export type EngineRunState = "stopped" | "playing" | "paused";
export type ResizeAxis = "horizontal" | "vertical";

export interface ToolbarCallbacks {
  onAction: (action: ToolbarAction) => void;
}

export interface PanelActionCallbacks {
  onClose?: (panelId: PanelId) => void;
  onDetach?: (panelId: PanelId) => void;
}
