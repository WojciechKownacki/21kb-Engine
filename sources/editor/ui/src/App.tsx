import { useCallback, useEffect, useMemo, useState, type ReactNode } from "react";

import {
  createDockWorkspace,
  deserializeDockWorkspace,
  dockPanel,
  floatPanel,
  serializeDockWorkspace,
  split,
  tabs,
  type DockWorkspace,
  type PanelId,
} from "./docking";
import { transitionEngineRunState, toolbarActionFromKey } from "./editor-controls";
import { AppShell, DockWorkspaceView, EngineToolbar, PanelFrame } from "./components";
import type { EngineRunState, ToolbarAction } from "./ui-types";
import { hasNativeEditorHost, openDetachedPanelWindow, openViewportScreen, type DetachablePanelId } from "./viewport";
import "./styles/editor-content.css";

const workspaceStorageKey = "kb.editor.workspace.v1";
const panelDefinitions = [
  { id: "project-files", minimumSize: { width: 220, height: 180 } },
  { id: "scene-objects", minimumSize: { width: 240, height: 180 } },
  { id: "details", minimumSize: { width: 260, height: 220 } },
  { id: "game-view", minimumSize: { width: 480, height: 320 } },
  { id: "console", minimumSize: { width: 360, height: 160 } },
] as const;

const panelTitles: Readonly<Record<string, string>> = {
  "project-files": "Project Files",
  "scene-objects": "Scene Objects",
  details: "Details",
  "game-view": "Game View",
  console: "Console",
};

function defaultWorkspace(): DockWorkspace {
  return createDockWorkspace({
    panels: panelDefinitions,
    root: split(
      "root",
      "horizontal",
      0.22,
      tabs("project", ["project-files"]),
      split(
        "content",
        "horizontal",
        0.68,
        split("viewport", "vertical", 0.74, tabs("game", ["game-view"]), tabs("console", ["console"])),
        split("inspector", "vertical", 0.44, tabs("scene", ["scene-objects"]), tabs("details", ["details"])),
      ),
    ),
    nextNodeId: 1,
  });
}

function loadWorkspace(): { workspace: DockWorkspace; warning: string | null } {
  const serialized = window.localStorage.getItem(workspaceStorageKey);
  if (serialized === null) {
    return { workspace: defaultWorkspace(), warning: null };
  }
  try {
    return { workspace: deserializeDockWorkspace(serialized), warning: null };
  } catch (error) {
    return {
      workspace: defaultWorkspace(),
      warning: `Saved workspace was rejected and reset: ${error instanceof Error ? error.message : String(error)}`,
    };
  }
}

function findFirstTabId(workspace: DockWorkspace): string {
  const find = (node: NonNullable<DockWorkspace["root"]>): string => node.kind === "tabs" ? node.id : find(node.first);
  if (workspace.root === null) {
    throw new Error("Cannot restore a floating panel into an empty workspace.");
  }
  return find(workspace.root);
}

function isDetachablePanelId(panelId: PanelId): panelId is DetachablePanelId {
  return panelId === "project-files" || panelId === "scene-objects" || panelId === "details" || panelId === "console";
}

function EditorEmptyState({ title, detail }: { title: string; detail: string }) {
  return <div className="editor-empty"><div><p className="editor-empty__title">{title}</p><p className="editor-empty__detail">{detail}</p></div></div>;
}

function createPanelContent(runtimeStatus: string, runtimeError: string | null): Readonly<Record<PanelId, ReactNode>> {
  return {
    "project-files": (
      <div className="editor-list" aria-label="Project file roots">
        <div className="editor-list__row"><span className="editor-list__icon" aria-hidden="true">◆</span>Project root is not selected</div>
        <div className="editor-list__row"><span className="editor-list__icon" aria-hidden="true">◇</span>Use the project host to open an asset directory</div>
      </div>
    ),
    "scene-objects": <EditorEmptyState title="No scene is loaded" detail="Scene Objects will populate from the runtime scene session." />,
    details: (
      <dl className="editor-properties">
        <div className="editor-properties__row"><dt>Selection</dt><dd>None</dd></div>
        <div className="editor-properties__row"><dt>Transform</dt><dd>Unavailable</dd></div>
        <div className="editor-properties__row"><dt>Components</dt><dd>0</dd></div>
      </dl>
    ),
    "game-view": (
      <div className="native-viewport-slot" data-viewport-host="primary" aria-label="Game View native viewport host">
        <div><p className="native-viewport-slot__label">Native viewport host</p><p className="native-viewport-slot__detail">{runtimeError ?? runtimeStatus}</p></div>
      </div>
    ),
    console: (
      <div className="editor-console" role="log" aria-live="polite">
        <div className="editor-console__line"><span className="editor-console__time">editor</span><span className="editor-console__channel">shell</span><span className="editor-console__message">Workspace host is ready.</span></div>
        <div className={`editor-console__line${runtimeError === null ? "" : " editor-console__line--error"}`}><span className="editor-console__time">runtime</span><span className="editor-console__channel">ipc</span><span className="editor-console__message">{runtimeError ?? runtimeStatus}</span></div>
      </div>
    ),
  };
}

function FloatingPanel({ panelId, content }: { panelId: PanelId; content: ReactNode }) {
  return <div className="floating-panel"><PanelFrame panelId={panelId} title={panelTitles[panelId] ?? panelId} active>{content}</PanelFrame></div>;
}

export function App() {
  const nativeHostAvailable = hasNativeEditorHost();
  const [loadedWorkspace] = useState(loadWorkspace);
  const [workspace, setWorkspace] = useState(loadedWorkspace.workspace);
  const [runState, setRunState] = useState<EngineRunState>("stopped");
  const [runtimeStatus, setRuntimeStatus] = useState(nativeHostAvailable
    ? "No native renderer is attached to this editor session."
    : "Browser preview mode: native windows and the Vulkan viewport require the desktop editor host.");
  const [runtimeError, setRuntimeError] = useState<string | null>(loadedWorkspace.warning);
  const [hostBusy, setHostBusy] = useState(false);

  useEffect(() => {
    try {
      window.localStorage.setItem(workspaceStorageKey, serializeDockWorkspace(workspace));
    } catch (error) {
      setRuntimeError(`Workspace persistence failed: ${error instanceof Error ? error.message : String(error)}`);
    }
  }, [workspace]);

  const content = useMemo(() => createPanelContent(runtimeStatus, runtimeError), [runtimeError, runtimeStatus]);
  const floatingPanelRoute = new URLSearchParams(window.location.search).get("panel");

  const handleAction = useCallback(async (action: ToolbarAction) => {
    if (action !== "screen") {
      setRunState((state) => transitionEngineRunState(state, action));
      setRuntimeStatus(`Editor control state changed to ${action}; a runtime controller must be registered before simulation can execute.`);
      return;
    }

    setHostBusy(true);
    try {
      const reply = await openViewportScreen();
      setRuntimeError(null);
      setRuntimeStatus(`Screen window ${reply.disposition}; viewport generation ${reply.surface.generation} is ready for renderer attachment.`);
    } catch (error) {
      setRuntimeError(`Screen request failed: ${error instanceof Error ? error.message : String(error)}`);
    } finally {
      setHostBusy(false);
    }
  }, []);

  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.defaultPrevented || event.repeat || event.target instanceof HTMLInputElement || event.target instanceof HTMLTextAreaElement) {
        return;
      }
      const action = toolbarActionFromKey(event.key);
      if (action === undefined) {
        return;
      }
      event.preventDefault();
      void handleAction(action);
    };
    window.addEventListener("keydown", onKeyDown);
    return () => window.removeEventListener("keydown", onKeyDown);
  }, [handleAction]);

  const handleDetach = useCallback(async (panelId: PanelId) => {
    setHostBusy(true);
    try {
      if (panelId === "game-view") {
        const reply = await openViewportScreen();
        setRuntimeStatus(`Game View moved to Screen (${reply.disposition}); generation ${reply.surface.generation}.`);
      } else if (isDetachablePanelId(panelId)) {
        const reply = await openDetachedPanelWindow(panelId);
        setRuntimeStatus(`${panelTitles[panelId]} window ${reply.disposition}.`);
      } else {
        throw new Error(`Panel '${panelId}' does not have a native host.`);
      }
      setWorkspace((current) => floatPanel(current, panelId, { bounds: { x: 80, y: 80, width: 480, height: panelId === "console" ? 320 : 640 } }));
      setRuntimeError(null);
    } catch (error) {
      setRuntimeError(`Could not detach ${panelTitles[panelId] ?? panelId}: ${error instanceof Error ? error.message : String(error)}`);
    } finally {
      setHostBusy(false);
    }
  }, []);

  const restoreFloatingPanel = useCallback((panelId: PanelId) => {
    setWorkspace((current) => dockPanel(current, panelId, { kind: "tab", tabId: findFirstTabId(current) }));
    setRuntimeStatus(`${panelTitles[panelId] ?? panelId} returned to the main workspace.`);
  }, []);

  if (floatingPanelRoute !== null) {
    if (!isDetachablePanelId(floatingPanelRoute)) {
      throw new Error(`Unsupported floating panel route '${floatingPanelRoute}'.`);
    }
    return <FloatingPanel panelId={floatingPanelRoute} content={content[floatingPanelRoute]} />;
  }

  return (
    <AppShell
      toolbar={<EngineToolbar state={runState} busy={hostBusy} screenAvailable={nativeHostAvailable} onAction={(action) => void handleAction(action)} />}
      status={
        <div className="workspace-status">
          {runtimeError ? <span className="workspace-status__error" title={runtimeError}>Host error</span> : <span>{runState}</span>}
          {workspace.floating.map((floating) => <button key={floating.id} className="workspace-status__floating" type="button" onClick={() => restoreFloatingPanel(floating.panelId)}>Dock {panelTitles[floating.panelId] ?? floating.panelId}</button>)}
        </div>
      }
    >
      <DockWorkspaceView workspace={workspace} content={content} onWorkspaceChange={setWorkspace} onDetach={nativeHostAvailable ? (panelId) => void handleDetach(panelId) : undefined} />
    </AppShell>
  );
}
