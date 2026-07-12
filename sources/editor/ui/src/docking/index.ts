/**
 * Immutable workspace model used by the editor shell.  Rendering, pointer
 * capture, and native window creation deliberately live outside this module.
 */

export const DOCK_WORKSPACE_VERSION = 1 as const;
export const SPLITTER_SIZE_PX = 6;

export type PanelId = string;
export type NodeId = string;
export type FloatingId = string;
export type SplitAxis = "horizontal" | "vertical";
export type DockEdge = "left" | "right" | "top" | "bottom";

export interface Size {
  readonly width: number;
  readonly height: number;
}

export interface Rectangle extends Size {
  readonly x: number;
  readonly y: number;
}

export interface DockPanelDefinition {
  readonly id: PanelId;
  readonly minimumSize: Size;
}

export interface TabsNode {
  readonly kind: "tabs";
  readonly id: NodeId;
  readonly panels: readonly PanelId[];
  readonly activePanelId: PanelId;
}

export interface SplitNode {
  readonly kind: "split";
  readonly id: NodeId;
  readonly axis: SplitAxis;
  /** Fraction of the usable split extent occupied by `first`. */
  readonly firstRatio: number;
  readonly first: DockNode;
  readonly second: DockNode;
}

export type DockNode = TabsNode | SplitNode;

export interface FloatingPanel {
  readonly id: FloatingId;
  readonly panelId: PanelId;
  readonly bounds: Rectangle;
}

export interface DockWorkspace {
  readonly version: typeof DOCK_WORKSPACE_VERSION;
  readonly panels: readonly DockPanelDefinition[];
  readonly root: DockNode | null;
  readonly floating: readonly FloatingPanel[];
  readonly nextNodeId: number;
  readonly nextFloatingId: number;
}

export interface CreateDockWorkspaceOptions {
  readonly panels: readonly DockPanelDefinition[];
  readonly root?: DockNode | null;
  readonly floating?: readonly FloatingPanel[];
  readonly nextNodeId?: number;
  readonly nextFloatingId?: number;
}

export type DockTarget =
  | { readonly kind: "tab"; readonly tabId: NodeId; readonly index?: number }
  | { readonly kind: "edge"; readonly nodeId: NodeId; readonly edge: DockEdge };

export interface ResizeSplitRequest {
  readonly splitId: NodeId;
  /** Desired size in pixels for the first child, excluding the splitter. */
  readonly firstExtent: number;
  /** Actual content rectangle allocated to this split node. */
  readonly availableSize: Size;
}

export interface FloatPanelRequest {
  readonly bounds: Rectangle;
  /** Supplying an id is useful when restoring a native window. */
  readonly floatingId?: FloatingId;
}

export function tabs(id: NodeId, panels: readonly PanelId[], activePanelId = panels[0]): TabsNode {
  if (!isNonEmptyString(id) || panels.length === 0 || !panels.every(isNonEmptyString) || !isNonEmptyString(activePanelId) || !panels.includes(activePanelId)) {
    throw new TypeError("A tab group requires a non-empty id, at least one panel, and an active contained panel.");
  }
  return { kind: "tabs", id, panels: [...panels], activePanelId: activePanelId as PanelId };
}

export function split(
  id: NodeId,
  axis: SplitAxis,
  firstRatio: number,
  first: DockNode,
  second: DockNode,
): SplitNode {
  if (!isNonEmptyString(id) || (axis !== "horizontal" && axis !== "vertical") || !Number.isFinite(firstRatio) || firstRatio <= 0 || firstRatio >= 1) {
    throw new TypeError("A split requires a non-empty id, a valid axis, and a ratio strictly between zero and one.");
  }
  return { kind: "split", id, axis, firstRatio, first, second };
}

/** Creates a fully validated, immutable-by-convention workspace. */
export function createDockWorkspace(options: CreateDockWorkspaceOptions): DockWorkspace {
  const panels = options.panels.map(copyPanelDefinition);
  assertPanelDefinitions(panels);
  const root = options.root === undefined
    ? (panels.length === 0 ? null : tabs("tabs-1", panels.map((panel) => panel.id)))
    : options.root === null ? null : copyNode(options.root);
  const workspace: DockWorkspace = {
    version: DOCK_WORKSPACE_VERSION,
    panels,
    root,
    floating: (options.floating ?? []).map(copyFloatingPanel),
    nextNodeId: options.nextNodeId ?? (root === null ? 1 : 2),
    nextFloatingId: options.nextFloatingId ?? 1,
  };
  assertWorkspace(workspace);
  return workspace;
}

export function minimumDockNodeSize(workspace: DockWorkspace, node: DockNode = requiredRoot(workspace)): Size {
  const definitions = panelDefinitionsById(workspace);
  return minimumNodeSize(node, definitions);
}

export function findDockNode(workspace: DockWorkspace, id: NodeId): DockNode | undefined {
  return workspace.root === null ? undefined : findNode(workspace.root, id);
}

export function dockPanel(workspace: DockWorkspace, panelId: PanelId, target: DockTarget): DockWorkspace {
  assertWorkspace(workspace);
  assertKnownPanel(workspace, panelId);
  const sourceTabs = workspace.root === null ? undefined : findTabsContaining(workspace.root, panelId);

  if (target.kind === "tab") {
    const destination = requireTabs(workspace, target.tabId);
    if (destination.panels.includes(panelId)) {
      return reorderWithinTabs(workspace, destination.id, panelId, target.index);
    }
    const detached = removePanel(workspace, panelId);
    const tabAfterRemoval = requireTabs(detached, target.tabId);
    const insertionIndex = target.index === undefined ? tabAfterRemoval.panels.length : target.index;
    if (!Number.isInteger(insertionIndex) || insertionIndex < 0 || insertionIndex > tabAfterRemoval.panels.length) {
      throw new RangeError(`Tab insertion index ${insertionIndex} is outside destination tab group.`);
    }
    const replacement = tabs(tabAfterRemoval.id, insertAt(tabAfterRemoval.panels, insertionIndex, panelId), panelId);
    return replaceWorkspaceNode(detached, replacement);
  }

  const destination = requireNode(workspace, target.nodeId);
  if (sourceTabs !== undefined && containsPanel(destination, panelId)) {
    throw new Error("A panel cannot be docked to an edge of a node that already contains it.");
  }
  const detached = removePanel(workspace, panelId);
  const destinationAfterRemoval = requireNode(detached, target.nodeId);
  const panelAllocation = allocateNodeId(detached);
  const splitAllocation = allocateNodeId(panelAllocation.workspace);
  const panelNode = tabs(panelAllocation.id, [panelId], panelId);
  const horizontal = target.edge === "left" || target.edge === "right";
  const placeFirst = target.edge === "left" || target.edge === "top";
  const replacement = split(
    splitAllocation.id,
    horizontal ? "horizontal" : "vertical",
    placeFirst ? 0.3 : 0.7,
    placeFirst ? panelNode : destinationAfterRemoval,
    placeFirst ? destinationAfterRemoval : panelNode,
  );
  return replaceWorkspaceNode({ ...splitAllocation.workspace, root: detached.root, floating: detached.floating }, replacement, destinationAfterRemoval.id);
}

/** Moves exactly one panel into an independently hosted native window. */
export function floatPanel(workspace: DockWorkspace, panelId: PanelId, request: FloatPanelRequest): DockWorkspace {
  assertWorkspace(workspace);
  const definition = panelDefinition(workspace, panelId);
  assertRectangle(request.bounds, "Floating bounds");
  assertFitsMinimum(request.bounds, definition.minimumSize, "Floating bounds");
  const detached = removePanel(workspace, panelId);
  const floatingId = request.floatingId ?? `floating-${detached.nextFloatingId}`;
  if (!isNonEmptyString(floatingId)) {
    throw new TypeError("Floating panel id must be a non-empty string.");
  }
  if (detached.floating.some((floating) => floating.id === floatingId)) {
    throw new Error(`Floating panel id '${floatingId}' already exists.`);
  }
  return checkedWorkspace({
    ...detached,
    floating: [...detached.floating, { id: floatingId, panelId, bounds: { ...request.bounds } }],
    nextFloatingId: request.floatingId === undefined ? detached.nextFloatingId + 1 : detached.nextFloatingId,
  });
}

export function setFloatingPanelBounds(workspace: DockWorkspace, floatingId: FloatingId, bounds: Rectangle): DockWorkspace {
  assertRectangle(bounds, "Floating bounds");
  const floating = workspace.floating.find((entry) => entry.id === floatingId);
  if (floating === undefined) {
    throw new Error(`Unknown floating panel '${floatingId}'.`);
  }
  assertFitsMinimum(bounds, panelDefinition(workspace, floating.panelId).minimumSize, "Floating bounds");
  return checkedWorkspace({
    ...workspace,
    floating: workspace.floating.map((entry) => entry.id === floatingId ? { ...entry, bounds: { ...bounds } } : entry),
  });
}

export function activatePanel(workspace: DockWorkspace, tabId: NodeId, panelId: PanelId): DockWorkspace {
  const destination = requireTabs(workspace, tabId);
  if (!destination.panels.includes(panelId)) {
    throw new Error(`Panel '${panelId}' is not present in tab group '${tabId}'.`);
  }
  return replaceWorkspaceNode(workspace, { ...destination, activePanelId: panelId });
}

/**
 * Resizes a split while preserving recursively calculated minimum dimensions.
 * A container smaller than its minima is an integration error, not a condition
 * hidden by negative geometry or an arbitrary fallback ratio.
 */
export function resizeSplit(workspace: DockWorkspace, request: ResizeSplitRequest): DockWorkspace {
  const node = requireNode(workspace, request.splitId);
  if (node.kind !== "split") {
    throw new Error(`Dock node '${request.splitId}' is not a split.`);
  }
  assertSize(request.availableSize, "Available split size");
  assertFiniteNonNegative(request.firstExtent, "Requested first extent");
  const extent = node.axis === "horizontal" ? request.availableSize.width : request.availableSize.height;
  const firstMinimum = nodeMinimumExtent(node.first, workspace, node.axis);
  const secondMinimum = nodeMinimumExtent(node.second, workspace, node.axis);
  const usableExtent = extent - SPLITTER_SIZE_PX;
  if (usableExtent < firstMinimum + secondMinimum) {
    throw new RangeError(`Split '${node.id}' has ${extent}px available but requires at least ${firstMinimum + secondMinimum + SPLITTER_SIZE_PX}px.`);
  }
  const firstExtent = clamp(request.firstExtent, firstMinimum, usableExtent - secondMinimum);
  return replaceWorkspaceNode(workspace, { ...node, firstRatio: firstExtent / usableExtent });
}

/** Stable, canonical persistence format. Equivalent workspaces always produce identical text. */
export function serializeDockWorkspace(workspace: DockWorkspace): string {
  assertWorkspace(workspace);
  return JSON.stringify({
    version: workspace.version,
    panels: [...workspace.panels].sort((left, right) => left.id.localeCompare(right.id)).map((panel) => ({
      id: panel.id,
      minimumSize: { width: panel.minimumSize.width, height: panel.minimumSize.height },
    })),
    root: workspace.root === null ? null : serializeNode(workspace.root),
    floating: [...workspace.floating].sort((left, right) => left.id.localeCompare(right.id)).map((floating) => ({
      id: floating.id,
      panelId: floating.panelId,
      bounds: { x: floating.bounds.x, y: floating.bounds.y, width: floating.bounds.width, height: floating.bounds.height },
    })),
    nextNodeId: workspace.nextNodeId,
    nextFloatingId: workspace.nextFloatingId,
  });
}

export function deserializeDockWorkspace(serialized: string): DockWorkspace {
  let parsed: unknown;
  try {
    parsed = JSON.parse(serialized) as unknown;
  } catch (error) {
    throw new SyntaxError(`Dock workspace is not valid JSON: ${error instanceof Error ? error.message : String(error)}`);
  }
  if (!isRecord(parsed)) {
    throw new TypeError("Dock workspace must be a JSON object.");
  }
  if (parsed.version !== DOCK_WORKSPACE_VERSION) {
    throw new Error(`Unsupported dock workspace version '${String(parsed.version)}'.`);
  }
  if (!Array.isArray(parsed.panels) || !Array.isArray(parsed.floating) || !isNonNegativeInteger(parsed.nextNodeId) || !isNonNegativeInteger(parsed.nextFloatingId)) {
    throw new TypeError("Dock workspace has an invalid top-level shape.");
  }
  const panels = parsed.panels.map(parsePanelDefinition);
  const root = parsed.root === null ? null : parseNode(parsed.root);
  const floating = parsed.floating.map(parseFloatingPanel);
  return createDockWorkspace({ panels, root, floating, nextNodeId: parsed.nextNodeId, nextFloatingId: parsed.nextFloatingId });
}

function reorderWithinTabs(workspace: DockWorkspace, tabId: NodeId, panelId: PanelId, index: number | undefined): DockWorkspace {
  const group = requireTabs(workspace, tabId);
  const oldIndex = group.panels.indexOf(panelId);
  const requested = index ?? group.panels.length - 1;
  if (!Number.isInteger(requested) || requested < 0 || requested >= group.panels.length) {
    throw new RangeError(`Tab insertion index ${requested} is outside destination tab group.`);
  }
  const withoutPanel = group.panels.filter((id) => id !== panelId);
  return replaceWorkspaceNode(workspace, {
    ...group,
    panels: insertAt(withoutPanel, requested, panelId),
    activePanelId: panelId,
  });
}

function removePanel(workspace: DockWorkspace, panelId: PanelId): DockWorkspace {
  const inRoot = workspace.root !== null && containsPanel(workspace.root, panelId);
  const floatingIndex = workspace.floating.findIndex((floating) => floating.panelId === panelId);
  if (!inRoot && floatingIndex === -1) {
    throw new Error(`Panel '${panelId}' is not currently placed in the workspace.`);
  }
  if (inRoot) {
    return { ...workspace, root: removePanelFromNode(workspace.root as DockNode, panelId) };
  }
  return { ...workspace, floating: workspace.floating.filter((_, index) => index !== floatingIndex) };
}

function removePanelFromNode(node: DockNode, panelId: PanelId): DockNode | null {
  if (node.kind === "tabs") {
    const panels = node.panels.filter((id) => id !== panelId);
    if (panels.length === node.panels.length) return node;
    if (panels.length === 0) return null;
    return { ...node, panels, activePanelId: node.activePanelId === panelId ? panels[0] : node.activePanelId };
  }
  const first = removePanelFromNode(node.first, panelId);
  const second = removePanelFromNode(node.second, panelId);
  if (first === node.first && second === node.second) return node;
  if (first === null) return second;
  if (second === null) return first;
  return { ...node, first, second };
}

function replaceWorkspaceNode(workspace: DockWorkspace, replacement: DockNode, targetId = replacement.id): DockWorkspace {
  if (workspace.root === null) throw new Error("Cannot replace a node in an empty dock tree.");
  return checkedWorkspace({ ...workspace, root: replaceNode(workspace.root, targetId, replacement) });
}

function replaceNode(node: DockNode, targetId: NodeId, replacement: DockNode): DockNode {
  if (node.id === targetId) return replacement;
  if (node.kind === "tabs") return node;
  const first = replaceNode(node.first, targetId, replacement);
  const second = replaceNode(node.second, targetId, replacement);
  return first === node.first && second === node.second ? node : { ...node, first, second };
}

function allocateNodeId(workspace: DockWorkspace): { readonly id: NodeId; readonly workspace: DockWorkspace } {
  const id = `node-${workspace.nextNodeId}`;
  return { id, workspace: { ...workspace, nextNodeId: workspace.nextNodeId + 1 } };
}

function requireNode(workspace: DockWorkspace, id: NodeId): DockNode {
  const node = findDockNode(workspace, id);
  if (node === undefined) throw new Error(`Unknown dock node '${id}'.`);
  return node;
}

function requireTabs(workspace: DockWorkspace, id: NodeId): TabsNode {
  const node = requireNode(workspace, id);
  if (node.kind !== "tabs") throw new Error(`Dock node '${id}' is not a tab group.`);
  return node;
}

function requiredRoot(workspace: DockWorkspace): DockNode {
  if (workspace.root === null) throw new Error("The workspace does not have a docked root.");
  return workspace.root;
}

function findNode(node: DockNode, id: NodeId): DockNode | undefined {
  if (node.id === id) return node;
  return node.kind === "split" ? findNode(node.first, id) ?? findNode(node.second, id) : undefined;
}

function findTabsContaining(node: DockNode, panelId: PanelId): TabsNode | undefined {
  if (node.kind === "tabs") return node.panels.includes(panelId) ? node : undefined;
  return findTabsContaining(node.first, panelId) ?? findTabsContaining(node.second, panelId);
}

function containsPanel(node: DockNode, panelId: PanelId): boolean {
  return node.kind === "tabs"
    ? node.panels.includes(panelId)
    : containsPanel(node.first, panelId) || containsPanel(node.second, panelId);
}

function minimumNodeSize(node: DockNode, definitions: ReadonlyMap<PanelId, DockPanelDefinition>): Size {
  if (node.kind === "tabs") {
    return node.panels.reduce<Size>((minimum, panelId) => {
      const panel = definitions.get(panelId);
      if (panel === undefined) throw new Error(`Missing definition for panel '${panelId}'.`);
      return { width: Math.max(minimum.width, panel.minimumSize.width), height: Math.max(minimum.height, panel.minimumSize.height) };
    }, { width: 0, height: 0 });
  }
  const first = minimumNodeSize(node.first, definitions);
  const second = minimumNodeSize(node.second, definitions);
  return node.axis === "horizontal"
    ? { width: first.width + SPLITTER_SIZE_PX + second.width, height: Math.max(first.height, second.height) }
    : { width: Math.max(first.width, second.width), height: first.height + SPLITTER_SIZE_PX + second.height };
}

function nodeMinimumExtent(node: DockNode, workspace: DockWorkspace, axis: SplitAxis): number {
  const minimum = minimumDockNodeSize(workspace, node);
  return axis === "horizontal" ? minimum.width : minimum.height;
}

function checkedWorkspace(workspace: DockWorkspace): DockWorkspace {
  assertWorkspace(workspace);
  return workspace;
}

function assertWorkspace(workspace: DockWorkspace): void {
  if (workspace.version !== DOCK_WORKSPACE_VERSION) throw new Error("Invalid dock workspace version.");
  assertPanelDefinitions(workspace.panels);
  if (!isNonNegativeInteger(workspace.nextNodeId) || !isNonNegativeInteger(workspace.nextFloatingId)) {
    throw new TypeError("Dock workspace counters must be non-negative integers.");
  }
  const definitions = panelDefinitionsById(workspace);
  const nodeIds = new Set<string>();
  const placedPanels = new Set<string>();
  if (workspace.root !== null) assertNode(workspace.root, definitions, nodeIds, placedPanels);
  const floatingIds = new Set<string>();
  for (const floating of workspace.floating) {
    if (!isNonEmptyString(floating.id) || floatingIds.has(floating.id)) throw new Error("Floating panel ids must be unique non-empty strings.");
    if (!definitions.has(floating.panelId) || placedPanels.has(floating.panelId)) throw new Error(`Panel '${floating.panelId}' must be placed exactly once.`);
    assertRectangle(floating.bounds, `Floating bounds for '${floating.id}'`);
    assertFitsMinimum(floating.bounds, definitions.get(floating.panelId)?.minimumSize as Size, `Floating bounds for '${floating.id}'`);
    floatingIds.add(floating.id);
    placedPanels.add(floating.panelId);
  }
  if (placedPanels.size !== definitions.size) throw new Error("Every registered panel must be placed exactly once.");
}

function assertNode(node: DockNode, definitions: ReadonlyMap<PanelId, DockPanelDefinition>, nodeIds: Set<string>, placedPanels: Set<string>): void {
  if (!isNonEmptyString(node.id) || nodeIds.has(node.id)) throw new Error("Dock node ids must be unique non-empty strings.");
  nodeIds.add(node.id);
  if (node.kind === "tabs") {
    if (node.panels.length === 0 || !node.panels.includes(node.activePanelId)) throw new Error(`Tab group '${node.id}' must have an active panel.`);
    for (const panelId of node.panels) {
      if (!definitions.has(panelId) || placedPanels.has(panelId)) throw new Error(`Panel '${panelId}' must be registered and placed exactly once.`);
      placedPanels.add(panelId);
    }
    return;
  }
  if ((node.axis !== "horizontal" && node.axis !== "vertical") || !Number.isFinite(node.firstRatio) || node.firstRatio <= 0 || node.firstRatio >= 1) {
    throw new Error(`Split '${node.id}' has invalid axis or ratio.`);
  }
  assertNode(node.first, definitions, nodeIds, placedPanels);
  assertNode(node.second, definitions, nodeIds, placedPanels);
}

function assertPanelDefinitions(panels: readonly DockPanelDefinition[]): void {
  const ids = new Set<string>();
  for (const panel of panels) {
    if (!isNonEmptyString(panel.id) || ids.has(panel.id)) throw new Error("Panel ids must be unique non-empty strings.");
    assertSize(panel.minimumSize, `Minimum size for panel '${panel.id}'`);
    ids.add(panel.id);
  }
}

function panelDefinitionsById(workspace: DockWorkspace): ReadonlyMap<PanelId, DockPanelDefinition> {
  return new Map(workspace.panels.map((panel) => [panel.id, panel]));
}

function panelDefinition(workspace: DockWorkspace, id: PanelId): DockPanelDefinition {
  const panel = panelDefinitionsById(workspace).get(id);
  if (panel === undefined) throw new Error(`Unknown panel '${id}'.`);
  return panel;
}

function assertKnownPanel(workspace: DockWorkspace, panelId: PanelId): void {
  panelDefinition(workspace, panelId);
}

function assertSize(value: Size, label: string): void {
  assertFiniteNonNegative(value.width, `${label} width`);
  assertFiniteNonNegative(value.height, `${label} height`);
}

function assertRectangle(value: Rectangle, label: string): void {
  if (!Number.isFinite(value.x) || !Number.isFinite(value.y)) throw new TypeError(`${label} position must be finite.`);
  assertSize(value, label);
}

function assertFitsMinimum(bounds: Size, minimum: Size, label: string): void {
  if (bounds.width < minimum.width || bounds.height < minimum.height) {
    throw new RangeError(`${label} is smaller than its panel minimum of ${minimum.width}x${minimum.height}.`);
  }
}

function assertFiniteNonNegative(value: number, label: string): void {
  if (!Number.isFinite(value) || value < 0) throw new TypeError(`${label} must be a finite non-negative number.`);
}

function isNonNegativeInteger(value: unknown): value is number {
  return typeof value === "number" && Number.isInteger(value) && value >= 0;
}

function isNonEmptyString(value: unknown): value is string {
  return typeof value === "string" && value.length > 0;
}

function clamp(value: number, minimum: number, maximum: number): number {
  return Math.max(minimum, Math.min(maximum, value));
}

function insertAt<T>(items: readonly T[], index: number, item: T): T[] {
  return [...items.slice(0, index), item, ...items.slice(index)];
}

function copyPanelDefinition(panel: DockPanelDefinition): DockPanelDefinition {
  return { id: panel.id, minimumSize: { ...panel.minimumSize } };
}

function copyFloatingPanel(floating: FloatingPanel): FloatingPanel {
  return { id: floating.id, panelId: floating.panelId, bounds: { ...floating.bounds } };
}

function copyNode(node: DockNode): DockNode {
  return node.kind === "tabs"
    ? { ...node, panels: [...node.panels] }
    : { ...node, first: copyNode(node.first), second: copyNode(node.second) };
}

function serializeNode(node: DockNode): object {
  return node.kind === "tabs"
    ? { kind: node.kind, id: node.id, panels: [...node.panels], activePanelId: node.activePanelId }
    : { kind: node.kind, id: node.id, axis: node.axis, firstRatio: node.firstRatio, first: serializeNode(node.first), second: serializeNode(node.second) };
}

function parsePanelDefinition(value: unknown): DockPanelDefinition {
  if (!isRecord(value) || !isNonEmptyString(value.id) || !isRecord(value.minimumSize) || typeof value.minimumSize.width !== "number" || typeof value.minimumSize.height !== "number") {
    throw new TypeError("Dock workspace contains an invalid panel definition.");
  }
  return { id: value.id, minimumSize: { width: value.minimumSize.width, height: value.minimumSize.height } };
}

function parseFloatingPanel(value: unknown): FloatingPanel {
  if (!isRecord(value) || !isNonEmptyString(value.id) || !isNonEmptyString(value.panelId) || !isRecord(value.bounds) || !isNumberRectangle(value.bounds)) {
    throw new TypeError("Dock workspace contains an invalid floating panel.");
  }
  return { id: value.id, panelId: value.panelId, bounds: { x: value.bounds.x, y: value.bounds.y, width: value.bounds.width, height: value.bounds.height } };
}

function parseNode(value: unknown): DockNode {
  if (!isRecord(value) || !isNonEmptyString(value.id) || (value.kind !== "tabs" && value.kind !== "split")) {
    throw new TypeError("Dock workspace contains an invalid dock node.");
  }
  if (value.kind === "tabs") {
    if (!Array.isArray(value.panels) || !value.panels.every(isNonEmptyString) || !isNonEmptyString(value.activePanelId)) throw new TypeError("Dock workspace contains an invalid tab group.");
    return tabs(value.id, value.panels, value.activePanelId);
  }
  if ((value.axis !== "horizontal" && value.axis !== "vertical") || typeof value.firstRatio !== "number") throw new TypeError("Dock workspace contains an invalid split.");
  return split(value.id, value.axis, value.firstRatio, parseNode(value.first), parseNode(value.second));
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function isNumberRectangle(value: Record<string, unknown>): value is Record<"x" | "y" | "width" | "height", number> {
  return typeof value.x === "number" && typeof value.y === "number" && typeof value.width === "number" && typeof value.height === "number";
}
