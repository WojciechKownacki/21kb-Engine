import assert from "node:assert/strict";
import test from "node:test";

import { resolveLeafDropIntent, resolveRootDropIntent } from "../src/docking/drop-intent.js";

const bounds = { x: 0, y: 0, width: 800, height: 600 };

test("root perimeter resolves one dominant 25 percent edge preview", () => {
  assert.deepEqual(resolveRootDropIntent(bounds, 8, 8), { kind: "edge", edge: "left", previewFraction: 0.25 });
  assert.deepEqual(resolveRootDropIntent(bounds, 400, 599), { kind: "edge", edge: "bottom", previewFraction: 0.25 });
  assert.equal(resolveRootDropIntent(bounds, 400, 300), null);
});

test("leaf header tabs while content edge bands create one half-size split preview", () => {
  assert.deepEqual(resolveLeafDropIntent(bounds, 400, 20, 38), { kind: "tab" });
  assert.deepEqual(resolveLeafDropIntent(bounds, 799, 300, 38), { kind: "edge", edge: "right", previewFraction: 0.5 });
  assert.equal(resolveLeafDropIntent(bounds, 400, 300, 38), null);
});
