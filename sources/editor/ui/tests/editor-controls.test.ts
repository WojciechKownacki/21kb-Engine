import assert from "node:assert/strict";
import test from "node:test";

import { toolbarActionFromKey, transitionEngineRunState } from "../src/editor-controls.js";

test("maps the editor control shortcuts without capturing unrelated keys", () => {
  assert.equal(toolbarActionFromKey("F5"), "play");
  assert.equal(toolbarActionFromKey("F6"), "pause");
  assert.equal(toolbarActionFromKey("F8"), "stop");
  assert.equal(toolbarActionFromKey("F11"), "screen");
  assert.equal(toolbarActionFromKey("Enter"), undefined);
});

test("applies only valid local control-state transitions", () => {
  assert.equal(transitionEngineRunState("stopped", "pause"), "stopped");
  assert.equal(transitionEngineRunState("stopped", "play"), "playing");
  assert.equal(transitionEngineRunState("playing", "pause"), "paused");
  assert.equal(transitionEngineRunState("paused", "stop"), "stopped");
});
