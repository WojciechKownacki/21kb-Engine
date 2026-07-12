import type { KeyboardEvent } from "react";

import type { EngineRunState, ToolbarAction, ToolbarCallbacks } from "../ui-types";

interface EngineToolbarProps extends ToolbarCallbacks {
  state: EngineRunState;
  busy?: boolean;
  screenAvailable?: boolean;
}

interface ToolbarButtonProps {
  action: ToolbarAction;
  label: string;
  shortcut: string;
  disabled?: boolean;
  active?: boolean;
  onAction: (action: ToolbarAction) => void;
}

function ToolbarButton({ action, label, shortcut, disabled, active, onAction }: ToolbarButtonProps) {
  return (
    <button
      className={`engine-toolbar__button engine-toolbar__button--${action}${active ? " is-active" : ""}`}
      type="button"
      disabled={disabled}
      aria-pressed={action === "play" || action === "pause" ? active : undefined}
      aria-keyshortcuts={shortcut}
      title={`${label} (${shortcut})`}
      onClick={() => onAction(action)}
    >
      <span className={`engine-toolbar__glyph engine-toolbar__glyph--${action}`} aria-hidden="true" />
      <span className="engine-toolbar__label">{label}</span>
    </button>
  );
}

/** Engine controls are controlled by the IPC-facing parent. */
export function EngineToolbar({ state, busy = false, screenAvailable = true, onAction }: EngineToolbarProps) {
  const onKeyDown = (event: KeyboardEvent<HTMLElement>) => {
    const action = event.key === "F5"
      ? "play"
      : event.key === "F6"
        ? "pause"
        : event.key === "F8"
          ? "stop"
          : event.key === "F11"
            ? "screen"
            : undefined;

    if (!action || busy || (action === "screen" && !screenAvailable)) {
      return;
    }

    event.preventDefault();
    onAction(action);
  };

  return (
    <nav className="engine-toolbar" aria-label="Engine controls" onKeyDown={onKeyDown}>
      <ToolbarButton action="play" label="Play" shortcut="F5" active={state === "playing"} disabled={busy || state === "playing"} onAction={onAction} />
      <ToolbarButton action="pause" label="Pause" shortcut="F6" active={state === "paused"} disabled={busy || state === "stopped"} onAction={onAction} />
      <ToolbarButton action="stop" label="Stop" shortcut="F8" disabled={busy || state === "stopped"} onAction={onAction} />
      <span className="engine-toolbar__divider" aria-hidden="true" />
      <ToolbarButton action="screen" label="Screen" shortcut="F11" disabled={busy || !screenAvailable} onAction={onAction} />
    </nav>
  );
}
