import { useMemo, useRef, useState, type CSSProperties, type DragEvent, type ReactNode } from "react";

import {
  SPLITTER_SIZE_PX,
  activatePanel,
  dockPanel,
  minimumDockNodeSize,
  resizeSplit,
  type DockNode,
  type DockWorkspace,
  type PanelId,
  type SplitNode,
  type TabsNode,
} from "../docking";
import { resolveLeafDropIntent, resolveRootDropIntent, type DockDropIntent } from "../docking/drop-intent";
import { PanelFrame } from "./PanelFrame";
import { ResizeHandle } from "./ResizeHandle";
import { TabStrip } from "./TabStrip";
import "../styles/dock-workspace.css";

export interface DockWorkspaceViewProps {
  workspace: DockWorkspace;
  content: Readonly<Record<PanelId, ReactNode>>;
  onWorkspaceChange: (workspace: DockWorkspace) => void;
  onDetach?: (panelId: PanelId) => void;
}

const panelTitles: Readonly<Record<string, string>> = {
  "project-files": "Project Files",
  "scene-objects": "Scene Objects",
  details: "Details",
  "game-view": "Game View",
  console: "Console",
};

function panelTitle(panelId: PanelId): string {
  return panelTitles[panelId] ?? panelId;
}

/** Renders the DockTree directly. The tree, rather than DOM geometry, owns layout state. */
export function DockWorkspaceView({ workspace, content, onWorkspaceChange, onDetach }: DockWorkspaceViewProps) {
  const [draggedPanelId, setDraggedPanelId] = useState<PanelId | null>(null);
  const [rootDropIntent, setRootDropIntent] = useState<DockDropIntent>(null);
  const rootDropIntentRef = useRef<DockDropIntent>(null);
  const completedDropRef = useRef(false);

  const dock = (panelId: PanelId, target: Parameters<typeof dockPanel>[2]) => {
    completedDropRef.current = true;
    onWorkspaceChange(dockPanel(workspace, panelId, target));
    rootDropIntentRef.current = null;
    setRootDropIntent(null);
    setDraggedPanelId(null);
  };

  const beginDrag = (event: DragEvent<HTMLElement>, panelId: PanelId) => {
    completedDropRef.current = false;
    event.dataTransfer.effectAllowed = "move";
    event.dataTransfer.setData("application/x-kb-editor-panel", panelId);
    setDraggedPanelId(panelId);
  };

  const endDrag = (event: DragEvent<HTMLElement>, panelId: PanelId) => {
    const wasDropped = completedDropRef.current || event.dataTransfer.dropEffect !== "none";
    completedDropRef.current = false;
    rootDropIntentRef.current = null;
    setRootDropIntent(null);
    setDraggedPanelId(null);
    if (!wasDropped) {
      onDetach?.(panelId);
    }
  };

  if (workspace.root === null) {
    throw new Error("The editor workspace cannot render without a docked root.");
  }
  const root = workspace.root;

  const resolveRootIntent = (event: DragEvent<HTMLDivElement>) => {
    if (draggedPanelId === null) return null;
    const bounds = event.currentTarget.getBoundingClientRect();
    return resolveRootDropIntent({ x: bounds.left, y: bounds.top, width: bounds.width, height: bounds.height }, event.clientX, event.clientY);
  };

  return (
    <div
      className="dock-workspace"
      aria-label="Editor workspace"
      onDragOverCapture={(event) => {
        const intent = resolveRootIntent(event);
        rootDropIntentRef.current = intent;
        setRootDropIntent(intent);
        if (intent !== null) event.preventDefault();
      }}
      onDropCapture={(event) => {
        const intent = rootDropIntentRef.current;
        if (intent?.kind !== "edge" || draggedPanelId === null) return;
        event.preventDefault();
        event.stopPropagation();
        dock(draggedPanelId, { kind: "edge", nodeId: root.id, edge: intent.edge });
      }}
    >
      {rootDropIntent?.kind === "edge" ? <DropPreview intent={rootDropIntent} root /> : null}
      <DockNodeView
        node={root}
        workspace={workspace}
        content={content}
        draggedPanelId={draggedPanelId}
        hasRootDropIntent={() => rootDropIntentRef.current !== null}
        onBeginDrag={beginDrag}
        onEndDrag={endDrag}
        onDock={dock}
        onDetach={onDetach}
        onWorkspaceChange={onWorkspaceChange}
      />
    </div>
  );
}

interface DockNodeViewProps {
  node: DockNode;
  workspace: DockWorkspace;
  content: Readonly<Record<PanelId, ReactNode>>;
  draggedPanelId: PanelId | null;
  hasRootDropIntent: () => boolean;
  onBeginDrag: (event: DragEvent<HTMLElement>, panelId: PanelId) => void;
  onEndDrag: (event: DragEvent<HTMLElement>, panelId: PanelId) => void;
  onDock: (panelId: PanelId, target: Parameters<typeof dockPanel>[2]) => void;
  onDetach?: (panelId: PanelId) => void;
  onWorkspaceChange: (workspace: DockWorkspace) => void;
}

function DockNodeView(props: DockNodeViewProps) {
  return props.node.kind === "split" ? <SplitView {...props} node={props.node} /> : <TabsView {...props} node={props.node} />;
}

function SplitView({ node, workspace, onWorkspaceChange, ...props }: DockNodeViewProps & { node: SplitNode }) {
  const containerRef = useRef<HTMLDivElement>(null);
  const isHorizontal = node.axis === "horizontal";
  const style = useMemo(() => {
    const minimum = minimumDockNodeSize(workspace, node);
    return {
      "--dock-first-ratio": String(node.firstRatio),
      minWidth: `${minimum.width}px`,
      minHeight: `${minimum.height}px`,
    } as CSSProperties;
  }, [node, workspace]);

  const onResize = (deltaPixels: number) => {
    const bounds = containerRef.current?.getBoundingClientRect();
    if (bounds === undefined) return;
    const extent = isHorizontal ? bounds.width : bounds.height;
    const currentFirstExtent = node.firstRatio * (extent - SPLITTER_SIZE_PX);
    try {
      onWorkspaceChange(resizeSplit(workspace, {
        splitId: node.id,
        firstExtent: currentFirstExtent + deltaPixels,
        availableSize: { width: bounds.width, height: bounds.height },
      }));
    } catch (error) {
      if (!(error instanceof RangeError)) throw error;
    }
  };

  return (
    <div ref={containerRef} className={`dock-split dock-split--${node.axis}`} style={style}>
      <div className="dock-split__first"><DockNodeView {...props} node={node.first} workspace={workspace} onWorkspaceChange={onWorkspaceChange} /></div>
      <ResizeHandle axis={isHorizontal ? "vertical" : "horizontal"} ariaLabel={`Resize ${node.id}`} onResize={onResize} />
      <div className="dock-split__second"><DockNodeView {...props} node={node.second} workspace={workspace} onWorkspaceChange={onWorkspaceChange} /></div>
    </div>
  );
}

function TabsView({ node, workspace, content, draggedPanelId, hasRootDropIntent, onBeginDrag, onEndDrag, onDock, onDetach, onWorkspaceChange }: DockNodeViewProps & { node: TabsNode }) {
  const [dropIntent, setDropIntent] = useState<DockDropIntent>(null);
  const activePanelId = node.activePanelId;
  const minimum = minimumDockNodeSize(workspace, node);
  const canAcceptDraggedPanel = draggedPanelId !== null && !node.panels.includes(draggedPanelId);
  const resolveIntent = (event: DragEvent<HTMLDivElement>) => {
    const bounds = event.currentTarget.getBoundingClientRect();
    return resolveLeafDropIntent({ x: bounds.left, y: bounds.top, width: bounds.width, height: bounds.height }, event.clientX, event.clientY, 38);
  };

  return (
    <div
      className="dock-tabs"
      style={{ minWidth: `${minimum.width}px`, minHeight: `${minimum.height}px` }}
      onDragOver={(event) => {
        if (!canAcceptDraggedPanel || hasRootDropIntent()) {
          setDropIntent(null);
          return;
        }
        const intent = resolveIntent(event);
        setDropIntent(intent);
        if (intent !== null) event.preventDefault();
      }}
      onDragLeave={(event) => {
        if (!event.currentTarget.contains(event.relatedTarget as Node | null)) setDropIntent(null);
      }}
      onDrop={(event) => {
        if (!canAcceptDraggedPanel || hasRootDropIntent()) return;
        const intent = resolveIntent(event);
        setDropIntent(null);
        if (intent === null) return;
        event.preventDefault();
        const panelId = event.dataTransfer.getData("application/x-kb-editor-panel") || draggedPanelId;
        if (panelId === null || panelId === "") return;
        onDock(panelId, intent.kind === "tab" ? { kind: "tab", tabId: node.id } : { kind: "edge", nodeId: node.id, edge: intent.edge });
      }}
    >
      {dropIntent !== null ? <DropPreview intent={dropIntent} /> : null}
      <div draggable onDragStart={(event) => onBeginDrag(event, activePanelId)} onDragEnd={(event) => onEndDrag(event, activePanelId)}>
        <PanelFrame
          panelId={activePanelId}
          title={panelTitle(activePanelId)}
          active
          tabs={node.panels.length > 1 ? <TabStrip tabs={node.panels.map((panelId) => ({ id: panelId, label: panelTitle(panelId) }))} activeTabId={activePanelId} ariaLabel={`${panelTitle(activePanelId)} dock tabs`} onActivate={(panelId) => onWorkspaceChange(activatePanel(workspace, node.id, panelId))} /> : undefined}
          onDetach={onDetach === undefined ? undefined : () => onDetach(activePanelId)}
        >
          {content[activePanelId]}
        </PanelFrame>
      </div>
    </div>
  );
}

function DropPreview({ intent, root = false }: { intent: Exclude<DockDropIntent, null>; root?: boolean }) {
  const style = intent.kind === "edge" ? ({ "--dock-preview-fraction": String(intent.previewFraction) } as CSSProperties) : undefined;
  return <div className={`dock-drop-preview${root ? " dock-drop-preview--root" : ""} dock-drop-preview--${intent.kind}${intent.kind === "edge" ? ` dock-drop-preview--${intent.edge}` : ""}`} style={style} aria-hidden="true" />;
}
