# Sprint 36 · 2D Subsystem

> Status: `BACKLOG`
>
> Delivery model: Outcome-based; no calendar deadline.
>
> Agent authorization: This checklist defines sprint scope. An agent may implement only a validated `READY` work item under the [Agent Execution Policy](../roadmap/AgentExecutionPolicy.md).


**Objective:** Deliver an integrated 2D production stack for sprites, atlases, tilemaps, lighting, cameras, physics, animation, effects, authoring, and batching while reusing shared engine services.

## 2D Rendering & Sprites

- [ ] Add a dedicated 2D rendering path
- [ ] Add sprite rendering with batching
- [ ] Add sprite pivots, flipping, and tinting
- [ ] Add sprite sorting layers and in-layer order
- [ ] Add a 2D draw order independent of 3D depth
- [ ] Add nine-slice and tiled sprite rendering
- [ ] Add sprite masking and stencil clipping
- [ ] Add 2D material and shader support
- [ ] Add additive, multiply, and custom 2D blend modes
- [ ] Add screen-space and world-space 2D rendering
- [ ] Add 2D render targets and post-processing
- [ ] Add 2D batching statistics and diagnostics

## Sprite Atlas & Assets

- [ ] Add a sprite-atlas asset that packs many sprites
- [ ] Add automatic atlas packing with padding
- [ ] Add sprite slicing from sheets (grid and auto-detect)
- [ ] Add per-sprite pivots, borders, and physics outlines
- [ ] Add multiple-atlas support and variants
- [ ] Add mip and filtering settings for pixel and smooth art
- [ ] Add atlas rebuild on source change
- [ ] Add atlas memory and packing reports

## Tilemaps

- [ ] Add a tilemap data structure and asset
- [ ] Add a tile palette and brush painting
- [ ] Add rectangular, isometric, and hexagonal grids
- [ ] Add multiple tilemap layers with ordering
- [ ] Add animated tiles
- [ ] Add rule tiles and auto-tiling
- [ ] Add random and weighted tile brushes
- [ ] Add tilemap collision generation
- [ ] Add chunked tilemaps for large levels
- [ ] Add tilemap streaming with the world
- [ ] Add a tilemap editor with paint, erase, fill, and select
- [ ] Add tilemap import and export
- [ ] Add tilemap rendering optimization and culling
- [ ] Add tilemap diagnostics

## 2D Lighting & Shadows

- [ ] Add 2D light sources (point, spot, directional, freeform)
- [ ] Add 2D normal-map lighting for sprites
- [ ] Add 2D shadow casting from shapes and sprites
- [ ] Add light blending and additive light layers
- [ ] Add global 2D ambient and day-night tinting
- [ ] Add light cookies and falloff shapes
- [ ] Add sprite self-illumination and emissive
- [ ] Add 2D lighting performance controls
- [ ] Add 2D lighting debug visualization

## 2D Cameras & Presentation

- [ ] Add an orthographic 2D camera
- [ ] Add pixel-perfect rendering and snapping
- [ ] Add resolution-independent 2D scaling
- [ ] Add camera follow, bounds, and dead zones
- [ ] Add parallax background layers
- [ ] Add 2D screen shake and effects
- [ ] Add multiple 2D cameras and split-screen
- [ ] Add safe-area handling for 2D UI overlap

## 2D Physics, Animation & Effects Integration

- [ ] Add integration with the 2D physics backend for sprites and tilemaps
- [ ] Add 2D collider generation from sprite outlines
- [ ] Add integration with 2D skeletal and cutout animation
- [ ] Add sprite-sheet flipbook animation playback
- [ ] Add 2D particle effects
- [ ] Add 2D trails and ribbons
- [ ] Add sorting integration between sprites, tilemaps, and particles
- [ ] Add 2D effect authoring

## 2D Authoring & Performance

- [ ] Add a 2D scene-editing mode
- [ ] Add sprite and tilemap placement tools
- [ ] Add sorting and layer management UI
- [ ] Add a 2D animation and state preview
- [ ] Add starter templates for platformers and top-down games
- [ ] Add 2D draw-call batching and atlas budgets
- [ ] Add 2D culling and off-screen skipping
- [ ] Add 2D performance profiling
- [ ] Add 2D subsystem tests
