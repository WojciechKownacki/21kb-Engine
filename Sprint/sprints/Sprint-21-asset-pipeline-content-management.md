# Sprint 21 · Asset Pipeline & Content Management

> Status: `BACKLOG`
>
> Delivery model: Outcome-based; no calendar deadline.
>
> Agent authorization: This checklist defines sprint scope. An agent may implement only a validated `READY` work item under the [Agent Execution Policy](../roadmap/AgentExecutionPolicy.md).


**Objective:** Deliver a versioned asset lifecycle from import and validation through registry, dependency tracking, editing, cooking, packaging, hot reload, migration, and runtime consumption.

## Import Framework

- [ ] Add a unified asset-import framework
- [ ] Add mesh import from standard interchange formats
- [ ] Add skeletal, animation, and morph import
- [ ] Add texture and image import with format detection
- [ ] Add audio and video import
- [ ] Add material and scene import from interchange formats
- [ ] Add per-importer settings and presets
- [ ] Add re-import that preserves overrides
- [ ] Add batch and folder import
- [ ] Add import validation and error reporting
- [ ] Add custom importer plugins
- [ ] Add import diagnostics and previews

## Asset Database & Registry

- [ ] Add a central asset registry keyed by stable identifiers
- [ ] Add identifier-to-asset resolution and loading
- [ ] Add reference counting and lifetime management
- [ ] Add dependency tracking between assets
- [ ] Add reverse-dependency and usage queries
- [ ] Add metadata and tags per asset
- [ ] Add asset search and filtering
- [ ] Add rename and move with reference fixup
- [ ] Add missing-asset and redirect handling
- [ ] Add asset validation and integrity checks
- [ ] Add asset versioning and migration
- [ ] Add registry diagnostics

## Content Browser

- [ ] Add a content browser with folders and thumbnails
- [ ] Add asset creation, duplication, and deletion
- [ ] Add drag-and-drop of assets into scenes and fields
- [ ] Add search, filters, and collections
- [ ] Add asset previews and inspection
- [ ] Add dependency and reference viewers
- [ ] Add favorites and recently used
- [ ] Add bulk operations and metadata editing
- [ ] Add source-control status indicators
- [ ] Add a beginner-friendly starter-content library

## Cooking & Build

- [ ] Add a cooking step that converts source assets to runtime formats
- [ ] Add per-platform asset cooking
- [ ] Add incremental cooking of changed assets
- [ ] Add a derived-data cache shared across builds
- [ ] Add asset bundling and packaging for shipping
- [ ] Add strip-and-optimize passes for runtime assets
- [ ] Add cook validation and reporting
- [ ] Add distributed and cached cooking

## Hot-Reload & Live Content

- [ ] Add source-asset file watching
- [ ] Add automatic re-import on source change
- [ ] Add hot-reload of textures, meshes, materials, and audio
- [ ] Add hot-reload of scenes and prefabs
- [ ] Add live update of running gameplay from asset changes
- [ ] Add safe fallback when a hot-reload fails
- [ ] Add hot-reload diagnostics
