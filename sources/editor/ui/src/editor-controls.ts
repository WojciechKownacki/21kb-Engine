import type { EngineRunState, ToolbarAction } from "./ui-types";

export function toolbarActionFromKey(key: string): ToolbarAction | undefined {
  switch (key) {
    case "F5": return "play";
    case "F6": return "pause";
    case "F8": return "stop";
    case "F11": return "screen";
    default: return undefined;
  }
}

/** Applies only valid editor-control transitions; invalid commands preserve state. */
export function transitionEngineRunState(state: EngineRunState, action: Exclude<ToolbarAction, "screen">): EngineRunState {
  switch (action) {
    case "play": return state === "playing" ? state : "playing";
    case "pause": return state === "playing" ? "paused" : state;
    case "stop": return "stopped";
  }
}
