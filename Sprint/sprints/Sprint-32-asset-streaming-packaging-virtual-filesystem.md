# Sprint 32 · Asset Streaming, Packaging & Virtual Filesystem

> Status: `BACKLOG`
>
> Delivery model: Outcome-based; no calendar deadline.
>
> Agent authorization: This checklist defines sprint scope. An agent may implement only a validated `READY` work item under the [Agent Execution Policy](../roadmap/AgentExecutionPolicy.md).


**Objective:** Deliver bounded asynchronous asset loading, residency, bundles, mounting, packaging, compression, encryption, and virtual-file access with stable handles, cancellation, diagnostics, and failure recovery.

## Background Asset-Processing Service

- [ ] Add a background asset-processing service that watches source content
- [ ] Add automatic processing of source assets into cooked runtime formats
- [ ] Add dependency-driven reprocessing when a source or dependency changes
- [ ] Add a processing job queue with priorities
- [ ] Add incremental processing of only changed assets
- [ ] Add parallel processing across worker threads
- [ ] Add distributed and shared processing across machines
- [ ] Add a shared derived-data cache to skip redundant work
- [ ] Add per-asset-type processors (mesh, texture, audio, material)
- [ ] Add processing status, progress, and error reporting
- [ ] Add retry and recovery for failed processing
- [ ] Add processing diagnostics and logs

## Asset Streaming, Loading & Handles

- [ ] Add asynchronous asset loading off the main thread
- [ ] Add addressable handles that reference assets by id or label
- [ ] Add load-by-address and load-by-label requests
- [ ] Add reference-counted load and release
- [ ] Add automatic loading of an asset's dependencies
- [ ] Add load priorities and queue ordering
- [ ] Add load progress reporting and callbacks
- [ ] Add cancellation of in-flight loads
- [ ] Add preloading and warmup of critical assets
- [ ] Add lazy and on-demand loading
- [ ] Add sub-asset and partial loading
- [ ] Add streaming of large assets (mesh, texture, audio) in chunks
- [ ] Add memory budgets and residency tracking for loaded assets
- [ ] Add eviction of unused assets under memory pressure
- [ ] Add placeholder and fallback assets while loading
- [ ] Add deterministic load ordering for tests
- [ ] Add a scripting API for loading and releasing assets
- [ ] Add streaming and loading diagnostics

## Asset Groups & Bundles

- [ ] Add asset groups that bundle related content
- [ ] Add labels and tags for addressing groups
- [ ] Add per-group build into loadable bundles
- [ ] Add local and remote (downloadable) group placement
- [ ] Add per-group compression and encryption settings
- [ ] Add cross-group dependency handling and deduplication
- [ ] Add a content catalog mapping addresses to bundles
- [ ] Add catalog updates for downloadable and patched content
- [ ] Add bundle load, unload, and reference counting
- [ ] Add bundle versioning and compatibility checks
- [ ] Add group and bundle authoring in the editor
- [ ] Add bundle build diagnostics and size reports

## Virtual Filesystem & Mounting

- [ ] Add a virtual filesystem over loose files and archives
- [ ] Add mount points with configurable priority
- [ ] Add mounting of pak/pack archive files
- [ ] Add overlay mounts where later mounts override earlier ones
- [ ] Add virtual-path resolution across all mounts
- [ ] Add loose-files-in-editor and archives-in-shipping modes
- [ ] Add read-only and writable mounts
- [ ] Add patch and downloadable-content overlay mounts
- [ ] Add case-insensitive and normalized path handling
- [ ] Add enumeration and globbing across mounts
- [ ] Add async file reads through the virtual filesystem
- [ ] Add memory-mapped reads for aligned archive entries
- [ ] Add mount lifecycle and hot-mount at runtime
- [ ] Add virtual-filesystem diagnostics

## Packaging, Compression & Encryption

- [ ] Add generation of pak/pack archive files from cooked content
- [ ] Add a directory index for fast lookup within archives
- [ ] Add per-file compression with selectable codecs
- [ ] Add block-based compression for random access
- [ ] Add entry alignment for memory-mapping
- [ ] Add encryption and signing of packaged content
- [ ] Add integrity checksums per entry and per archive
- [ ] Add chunked archives split by size or content group
- [ ] Add ordering of entries by load pattern for locality
- [ ] Add delta and patch archive generation
- [ ] Add on-demand and streamed archive download
- [ ] Add package validation and repair
- [ ] Add packaging size and compression reports
- [ ] Add packaging diagnostics

## Testing & Validation

- [ ] Add asynchronous-load correctness and cancellation tests
- [ ] Add reference-counting and eviction tests
- [ ] Add bundle build and dependency-deduplication tests
- [ ] Add virtual-filesystem path-resolution and override tests
- [ ] Add compression and decompression round-trip tests
- [ ] Add encryption and integrity-verification tests
- [ ] Add patch-overlay and catalog-update tests
- [ ] Add missing-asset and fallback-handling tests
- [ ] Add streaming memory-budget stress tests
- [ ] Add deterministic-load regression tests
