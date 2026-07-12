import assert from "node:assert/strict";
import test from "node:test";

import {
  DOCK_WORKSPACE_VERSION,
  SPLITTER_SIZE_PX,
  createDockWorkspace,
  deserializeDockWorkspace,
  dockPanel,
  findDockNode,
  floatPanel,
  minimumDockNodeSize,
  resizeSplit,
  serializeDockWorkspace,
  split,
  tabs,
} from "../src/docking/index.js";

const panels = [
  { id: "project", minimumSize: { width: 220, height: 180 } },
  { id: "scene", minimumSize: { width: 240, height: 180 } },
  { id: "details", minimumSize: { width: 260, height: 220 } },
  { id: "game", minimumSize: { width: 480, height: 320 } },
  { id: "console", minimumSize: { width: 360, height: 160 } },
] as const;

function workspace() {
  return createDockWorkspace({
    panels,
    root: split("root", "horizontal", 0.4, tabs("left", ["project", "scene"], "project"), tabs("main", ["game", "details", "console"], "game")),
    nextNodeId: 1,
  });
}

test("docks a panel to every edge without duplicating it", () => {
  for (const edge of ["left", "right", "top", "bottom"] as const) {
    const result = dockPanel(workspace(), "details", { kind: "edge", nodeId: "left", edge });
    assert.equal(countPlacedPanels(result.root, result.floating.map((entry) => entry.panelId), "details"), 1, edge);
    assert.equal(result.root?.kind, "split");
  }
});

test("moves a panel between tab groups and keeps it active", () => {
  const result = dockPanel(workspace(), "details", { kind: "tab", tabId: "left", index: 1 });
  const left = result.root?.kind === "split" ? result.root.first : undefined;
  assert.deepEqual(left, tabs("left", ["project", "details", "scene"], "details"));
});

test("floats and restores a panel through the same docking operation", () => {
  const floating = floatPanel(workspace(), "game", { bounds: { x: 40, y: 50, width: 800, height: 600 } });
  assert.deepEqual(floating.floating[0]?.id, "floating-1");
  assert.equal(floating.root?.kind, "split");
  const restored = dockPanel(floating, "game", { kind: "tab", tabId: "main" });
  assert.equal(restored.floating.length, 0);
  assert.deepEqual(findDockNode(restored, "main"), tabs("main", ["details", "console", "game"], "game"));
});

test("minimum sizes compose recursively and constrain split resizing", () => {
  const layout = workspace();
  assert.deepEqual(minimumDockNodeSize(layout), { width: 726, height: 320 });
  const result = resizeSplit(layout, { splitId: "root", firstExtent: 1, availableSize: { width: 1_000, height: 500 } });
  assert.equal(result.root?.kind, "split");
  assert.equal(result.root?.kind === "split" ? result.root.firstRatio : 0, 240 / (1_000 - SPLITTER_SIZE_PX));
  assert.throws(() => resizeSplit(layout, { splitId: "root", firstExtent: 300, availableSize: { width: 700, height: 500 } }), /requires at least/);
});

test("serializes canonically and restores the complete workspace", () => {
  const original = floatPanel(workspace(), "console", { floatingId: "console-window", bounds: { x: -120, y: 20, width: 600, height: 300 } });
  const serialized = serializeDockWorkspace(original);
  const restored = deserializeDockWorkspace(serialized);
  assert.equal(serializeDockWorkspace(restored), serialized);
  assert.match(serialized, new RegExp(`\"version\":${DOCK_WORKSPACE_VERSION}`));
});

test("rejects impossible floating geometry and malformed persisted state", () => {
  assert.throws(() => floatPanel(workspace(), "game", { bounds: { x: 0, y: 0, width: 400, height: 300 } }), /smaller than/);
  assert.throws(() => deserializeDockWorkspace('{"version":1}'), /invalid top-level shape/);
});

function countPlacedPanels(node: ReturnType<typeof workspace>["root"], floatingPanels: readonly string[], panelId: string): number {
  if (node === null) return floatingPanels.filter((id) => id === panelId).length;
  if (node.kind === "tabs") return node.panels.filter((id) => id === panelId).length + floatingPanels.filter((id) => id === panelId).length;
  return countPlacedPanels(node.first, floatingPanels, panelId) + countPlacedPanels(node.second, [], panelId);
}
