import type { DockEdge, Rectangle } from "./index";

export type DockDropIntent =
  | { readonly kind: "tab" }
  | { readonly kind: "edge"; readonly edge: DockEdge; readonly previewFraction: number }
  | null;

export const ROOT_EDGE_BAND_PX = 30;
export const ROOT_EDGE_PREVIEW_FRACTION = 0.25;
export const LEAF_EDGE_BAND_FRACTION = 0.4;
export const LEAF_EDGE_PREVIEW_FRACTION = 0.5;

/** Mirrors Verth's root-edge policy: a narrow perimeter creates a 25% root split. */
export function resolveRootDropIntent(bounds: Rectangle, x: number, y: number): DockDropIntent {
  const edge = dominantEdge(bounds, x, y, ROOT_EDGE_BAND_PX);
  return edge === null ? null : { kind: "edge", edge, previewFraction: ROOT_EDGE_PREVIEW_FRACTION };
}

/**
 * Mirrors Verth's leaf policy. Header drops become tabs; content edge bands
 * produce a 50/50 split. Dropping in content centre intentionally has no
 * target, so a detached window stays where the user leaves it.
 */
export function resolveLeafDropIntent(bounds: Rectangle, x: number, y: number, stripHeight: number): DockDropIntent {
  if (!contains(bounds, x, y)) {
    return null;
  }
  if (y < bounds.y + Math.min(stripHeight, bounds.height)) {
    return { kind: "tab" };
  }
  const band = Math.min(bounds.width, bounds.height) * LEAF_EDGE_BAND_FRACTION;
  const edge = dominantEdge(bounds, x, y, band);
  return edge === null ? null : { kind: "edge", edge, previewFraction: LEAF_EDGE_PREVIEW_FRACTION };
}

function dominantEdge(bounds: Rectangle, x: number, y: number, band: number): DockEdge | null {
  if (!contains(bounds, x, y) || band <= 0) {
    return null;
  }
  const depths: ReadonlyArray<readonly [DockEdge, number]> = [
    ["left", x < bounds.x + band ? bounds.x + band - x : -1],
    ["right", x >= bounds.x + bounds.width - band ? x - (bounds.x + bounds.width - band) : -1],
    ["top", y < bounds.y + band ? bounds.y + band - y : -1],
    ["bottom", y >= bounds.y + bounds.height - band ? y - (bounds.y + bounds.height - band) : -1],
  ];
  return depths.reduce<readonly [DockEdge, number] | null>((best, candidate) => candidate[1] > (best?.[1] ?? -1) ? candidate : best, null)?.[0] ?? null;
}

function contains(bounds: Rectangle, x: number, y: number): boolean {
  return x >= bounds.x && x < bounds.x + bounds.width && y >= bounds.y && y < bounds.y + bounds.height;
}
