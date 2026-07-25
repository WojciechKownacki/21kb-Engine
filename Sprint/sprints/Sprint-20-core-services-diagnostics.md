# Sprint 20 · Core Services & Diagnostics

> Status: `BACKLOG`
>
> Delivery model: Outcome-based; no calendar deadline.
>
> Agent authorization: This checklist defines sprint scope. An agent may implement only a validated `READY` work item under the [Agent Execution Policy](../roadmap/AgentExecutionPolicy.md).


**Objective:** Deliver shared profiling, tracing, compression, cryptography, randomness, time, testing, benchmarking, and task-graph services that provide production diagnostics and reusable infrastructure to all subsystems.

## CPU Profiling, Tracing & Instrumentation

- [ ] Add named CPU profiling scopes
- [ ] Add a hierarchical frame timeline across subsystems
- [ ] Add a flame-graph and call-tree view
- [ ] Add per-thread timelines
- [ ] Add counters and custom plot values
- [ ] Add a low-overhead ring-buffer trace format
- [ ] Add export to standard trace viewers
- [ ] Add a live in-engine profiler overlay
- [ ] Add markers correlated with GPU timings
- [ ] Add allocation and lock instrumentation
- [ ] Add sampling and instrumented profiling modes
- [ ] Add profiler-cost budgets and toggles

## Compression Codecs

- [ ] Add a general-purpose fast compression codec
- [ ] Add a high-ratio compression codec
- [ ] Add streaming compression and decompression
- [ ] Add block and chunked compression for random access
- [ ] Add a codec abstraction with selectable backends
- [ ] Add hardware-accelerated decompression where available
- [ ] Add compression-level and dictionary configuration
- [ ] Add integration with assets, streaming, saves, and networking
- [ ] Add compression benchmarks and diagnostics

## Cryptography & Secure Hashing

- [ ] Add symmetric encryption for data at rest
- [ ] Add authenticated encryption with integrity tags
- [ ] Add secure cryptographic hashing
- [ ] Add signing and signature verification
- [ ] Add key derivation and secure random generation
- [ ] Add certificate and token handling
- [ ] Add tamper detection for saves and packaged content
- [ ] Add a secure-storage abstraction for secrets
- [ ] Add cryptography validation and test vectors

## Random & Noise Utilities

- [ ] Add a fast general-purpose pseudo-random generator
- [ ] Add multiple seedable random streams
- [ ] Add uniform, normal, and weighted distributions
- [ ] Add deterministic and reproducible sequences
- [ ] Add a shared noise library reused across systems
- [ ] Add random utilities exposed to gameplay and scripting

## Date, Time & Calendar

- [ ] Add wall-clock date and time types
- [ ] Add time-zone handling and conversion
- [ ] Add durations and interval arithmetic
- [ ] Add formatting and parsing of dates and times
- [ ] Add culture-aware date and time formatting
- [ ] Add real-world time queries for live features
- [ ] Add monotonic and calendar clock separation

## Unit-Test & Benchmark Framework

- [ ] Add an engine-wide unit-test framework
- [ ] Add assertions, fixtures, and parameterized tests
- [ ] Add a micro-benchmark framework with statistics
- [ ] Add a headless test runner
- [ ] Add test discovery and filtering
- [ ] Add golden-data and snapshot testing helpers
- [ ] Add continuous-integration reporting output
- [ ] Add flaky-test detection and retries
- [ ] Add coverage instrumentation hooks

## Task Dependency Graph

- [ ] Add a declarative task dependency graph over the job system
- [ ] Add fork-join and pipeline task patterns
- [ ] Add cross-frame and persistent task graphs
- [ ] Add priorities and affinities per task
- [ ] Add cancellation and error propagation
- [ ] Add graph visualization and profiling
- [ ] Add deterministic scheduling for tests
