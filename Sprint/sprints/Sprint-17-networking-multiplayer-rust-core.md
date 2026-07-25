# Sprint 17 · Networking / Multiplayer (Rust core)

> Status: `BACKLOG`
>
> Delivery model: Outcome-based; no calendar deadline.
>
> Agent authorization: This checklist defines sprint scope. An agent may implement only a validated `READY` work item under the [Agent Execution Policy](../roadmap/AgentExecutionPolicy.md).


**Objective:** Deliver a secure, versioned multiplayer stack with a stable Rust-to-C++ boundary, transport and session management, replication, prediction, rollback, authority, hosting, platform integration, simulation tools, diagnostics, and automated network testing.

## Rust Networking Core & FFI Boundary

- [ ] Add a Rust networking crate as the standalone network core
- [ ] Add a stable C-ABI boundary between the Rust core and the C++ engine
- [ ] Add a build integration that compiles the Rust crate into the engine
- [ ] Add typed opaque handles for connections, channels, and sessions across the boundary
- [ ] Add clear ownership and lifetime rules for buffers crossing the boundary
- [ ] Add zero-copy buffer passing where possible
- [ ] Add panic isolation so a Rust panic never unwinds into C++
- [ ] Add an async runtime (Tokio) owned by the network core
- [ ] Add a command queue from the engine thread into the network core
- [ ] Add an event queue from the network core back to the engine
- [ ] Add thread-safe, lock-light interchange between engine and network threads
- [ ] Add ABI versioning and compatibility checks
- [ ] Add error codes and diagnostics across the boundary
- [ ] Add memory-tracker integration for Rust allocations
- [ ] Add a headless build of the core for dedicated servers
- [ ] Add a WebAssembly build of the core for browser clients
- [ ] Add logging bridged from Rust into engine diagnostics
- [ ] Add configuration passed from the engine to the core

## Transport Layer

- [ ] Add a QUIC transport as the primary protocol
- [ ] Add TLS encryption and authentication via QUIC
- [ ] Add multiple QUIC streams for independent channels
- [ ] Add QUIC datagram support for unreliable traffic
- [ ] Add congestion control and pacing
- [ ] Add path-MTU discovery
- [ ] Add segmentation-offload use where the platform supports it
- [ ] Add a reliable-UDP transport as an alternative
- [ ] Add a connection protocol with secure tokens and key exchange
- [ ] Add a WebTransport backend for browser clients
- [ ] Add a WebSocket fallback transport
- [ ] Add a platform-socket backend (console and store networking)
- [ ] Add NAT traversal and hole punching
- [ ] Add relay and fallback routing when direct connection fails
- [ ] Add per-transport capability negotiation
- [ ] Add transport selection and automatic fallback
- [ ] Add packet fragmentation and reassembly
- [ ] Add transport-level statistics (throughput, loss, RTT)

## Connection & Session Management

- [ ] Add connect and disconnect flows
- [ ] Add a handshake with version and capability exchange
- [ ] Add authentication tokens and validation
- [ ] Add session lifecycle and identifiers
- [ ] Add keepalive and timeout handling
- [ ] Add graceful and abrupt disconnect handling
- [ ] Add automatic reconnection with backoff
- [ ] Add connection-quality metrics (RTT, jitter, loss)
- [ ] Add per-connection state and user data
- [ ] Add connection limits and admission control
- [ ] Add ban and kick support
- [ ] Add session migration between transports
- [ ] Add connection events surfaced to gameplay
- [ ] Add connection debugging tools

## Serialization & Wire Format

- [ ] Add a compact binary wire format
- [ ] Add zero-copy serialization for hot paths
- [ ] Add bit-packing of small fields and flags
- [ ] Add quantization of positions, rotations, and scales
- [ ] Add delta encoding against a baseline
- [ ] Add schema and version tags for messages
- [ ] Add forward- and backward-compatible message evolution
- [ ] Add endian-independent encoding
- [ ] Add optional compression for large payloads
- [ ] Add string interning and dictionary compression
- [ ] Add a code-generated schema from component reflection
- [ ] Add per-field precision and range annotations
- [ ] Add validation and bounds checking on decode
- [ ] Add fuzzing of the decoder against malformed input
- [ ] Add serialization benchmarks
- [ ] Add wire-format debugging and inspection

## ECS Replication Integration

- [ ] Map engine entities to stable network identifiers
- [ ] Add a bidirectional mapping between network ids and flecs entities
- [ ] Add server and client world roles
- [ ] Drive replication from component-change tracking (modified flags and changed filters)
- [ ] Add component observers feeding spawn, update, and despawn events
- [ ] Enumerate replicated fields generically through component reflection
- [ ] Add per-component replication opt-in and configuration
- [ ] Apply remote changes through the deferred command buffer
- [ ] Add deterministic entity-id remapping on the client
- [ ] Reuse the chunked world-snapshot codec for baselines
- [ ] Reuse delta snapshots for per-tick updates
- [ ] Add parent and reference remapping across the network
- [ ] Add replication of relationships, tags, and singletons
- [ ] Add batched, chunk-friendly replication reads
- [ ] Add SIMD-friendly packing of replicated component chunks
- [ ] Add a replication schedule within the system scheduler
- [ ] Add replication of structural changes (add/remove component)
- [ ] Add server-side authoritative apply and validation
- [ ] Add a replication registry mapping component types to codecs
- [ ] Add replication configuration as a data asset

## Entity & Component Replication

- [ ] Add server-authoritative entity spawn replication
- [ ] Add entity despawn replication
- [ ] Add per-component state replication
- [ ] Add replication groups and priorities
- [ ] Add replicate-on-change versus replicate-every-tick modes
- [ ] Add initial-state replication on join
- [ ] Add late-join catch-up replication
- [ ] Add replication of spawned prefabs and templates
- [ ] Add ownership tags carried with entities
- [ ] Add relevancy filtering per entity and client
- [ ] Add replication of transform with quantization and smoothing
- [ ] Add replication of animation and gameplay state
- [ ] Add replication frequency per component
- [ ] Add replication of arrays and dynamic buffers
- [ ] Add conditional replication by role and authority
- [ ] Add replication of destruction and pooled entities
- [ ] Add replication conflict resolution
- [ ] Add replication debugging per entity

## Snapshots & Delta Compression

- [ ] Add periodic world snapshots
- [ ] Add per-entity and per-component deltas against a baseline
- [ ] Add acknowledgement of received snapshots
- [ ] Add baseline management from acked snapshots
- [ ] Add a snapshot ring buffer of recent states
- [ ] Add delta encoding of only changed fields
- [ ] Add run-length and bit-mask encoding of change sets
- [ ] Add priority-based partial snapshots under bandwidth limits
- [ ] Add reliable baseline recovery after loss
- [ ] Add compression of snapshot payloads
- [ ] Add snapshot size budgets and diagnostics
- [ ] Add snapshot history for lag compensation and rollback
- [ ] Add deterministic snapshot ordering
- [ ] Add snapshot debugging and inspection

## Reliability Channels & Messaging

- [ ] Add a reliable-ordered channel
- [ ] Add a reliable-unordered channel
- [ ] Add an unreliable channel
- [ ] Add an unreliable-sequenced channel
- [ ] Add per-channel configuration and priority
- [ ] Add message fragmentation for large messages
- [ ] Add message batching and coalescing
- [ ] Add acknowledgement and retransmission for reliable channels
- [ ] Add ordering guarantees per channel
- [ ] Add flow control per channel
- [ ] Add channel statistics and diagnostics
- [ ] Add a typed message registry
- [ ] Add message priority and starvation avoidance
- [ ] Add channel debugging tools

## RPC & Events

- [ ] Add client-to-server remote procedure calls
- [ ] Add server-to-client remote procedure calls
- [ ] Add multicast remote procedure calls
- [ ] Add typed RPC parameters from reflection
- [ ] Add per-RPC reliability and channel selection
- [ ] Add request and response RPCs with correlation
- [ ] Add validation and rate limiting of incoming RPCs
- [ ] Add ordering guarantees relative to replication
- [ ] Add a scripting API for RPCs and network events
- [ ] Add deterministic RPC handling for replay
- [ ] Add RPC diagnostics and logging
- [ ] Add RPC schema versioning

## Input, Ticks & Clock Sync

- [ ] Add a fixed network tick decoupled from frame rate
- [ ] Add tick-buffered input channels
- [ ] Add delivery of input at the matching server tick
- [ ] Add input prediction and buffering
- [ ] Add clock synchronization between client and server
- [ ] Add round-trip and one-way delay estimation
- [ ] Add time-offset smoothing and drift correction
- [ ] Add tick adjustment to keep clients aligned
- [ ] Add input redundancy against packet loss
- [ ] Add input acknowledgement and resend
- [ ] Add server-side input validation and clamping
- [ ] Add deterministic input application order
- [ ] Add input-timeline debugging
- [ ] Add tick and clock diagnostics

## Client-Side Prediction & Reconciliation

- [ ] Add prediction of the locally controlled entity
- [ ] Add application of local input immediately
- [ ] Add storage of predicted state per tick
- [ ] Add reconciliation against authoritative snapshots
- [ ] Add rollback of predicted state on mismatch
- [ ] Add re-simulation from the corrected state
- [ ] Add prediction of owned non-player entities
- [ ] Add predicted spawning with server confirmation
- [ ] Add error smoothing to hide small corrections
- [ ] Add snap thresholds for large corrections
- [ ] Add prediction masks per component
- [ ] Add prediction of physics-driven entities
- [ ] Add reconciliation diagnostics and error metrics
- [ ] Add prediction debugging visualization
- [ ] Add deterministic prediction for tests
- [ ] Add integration with the command buffer for predicted changes

## Rollback Netcode

- [ ] Add a rollback simulation model for deterministic games
- [ ] Add input delay configuration
- [ ] Add confirmed-frame tracking
- [ ] Add save and restore of simulation state per frame
- [ ] Reuse world snapshots for fast state save and restore
- [ ] Add re-simulation on receiving remote input
- [ ] Add prediction of remote input until confirmed
- [ ] Add a rollback window and frame budget
- [ ] Add peer-to-peer lockstep synchronization
- [ ] Add a desync detection and sync-test mode
- [ ] Add checksum comparison across peers
- [ ] Add catch-up and frame-skipping under load
- [ ] Add deterministic math and iteration ordering hooks
- [ ] Add rollback of audio and visual effects
- [ ] Add rollback diagnostics and replay
- [ ] Add rollback determinism tests

## Snapshot Interpolation

- [ ] Add interpolation of remotely replicated entities
- [ ] Add an interpolation delay and buffer
- [ ] Add a snapshot buffer of recent remote states
- [ ] Add hermite and linear interpolation of transforms
- [ ] Add extrapolation when snapshots are late
- [ ] Add clamping and blending of extrapolation error
- [ ] Add per-entity interpolation configuration
- [ ] Add interpolation of custom replicated fields
- [ ] Add smoothing of teleports and discontinuities
- [ ] Add interpolation for animation and gameplay state
- [ ] Add interpolation diagnostics
- [ ] Add interpolation debugging visualization

## Authority & Ownership

- [ ] Add server authority over entities by default
- [ ] Add client authority for owned entities
- [ ] Add per-component authority configuration
- [ ] Add authority transfer between server and clients
- [ ] Add ownership assignment on spawn
- [ ] Add authority-aware replication and prediction
- [ ] Add conflict resolution when authority changes
- [ ] Add validation of client-authored state on the server
- [ ] Add authority events surfaced to gameplay
- [ ] Add authority debugging and inspection
- [ ] Add ownership persistence across reconnection
- [ ] Add authority tests

## Interest Management & Relevancy

- [ ] Add area-of-interest per client
- [ ] Add spatial culling of replication by distance
- [ ] Add relevancy queries integrated with the spatial structure
- [ ] Add priority scoring per entity and client
- [ ] Add streaming-integrated relevancy for large worlds
- [ ] Add team and faction relevancy rules
- [ ] Add always-relevant and never-relevant tagging
- [ ] Add relevancy hysteresis to avoid churn
- [ ] Add per-client relevancy budgets
- [ ] Add relevancy updates spread across ticks
- [ ] Add relevancy debugging visualization
- [ ] Add relevancy performance budgets
- [ ] Add scalable relevancy for many clients
- [ ] Add relevancy tests

## Bandwidth & Rate Control

- [ ] Add a per-client bandwidth budget
- [ ] Add a priority accumulator for fair replication
- [ ] Add rate limiting and shaping of outgoing traffic
- [ ] Add congestion-aware send scheduling
- [ ] Add adaptive quality under constrained bandwidth
- [ ] Add per-channel bandwidth allocation
- [ ] Add drop and defer policies under pressure
- [ ] Add measurement of available bandwidth
- [ ] Add compression trade-off tuning
- [ ] Add bandwidth statistics per client and channel
- [ ] Add over-budget diagnostics
- [ ] Add bandwidth stress tests

## Lag Compensation

- [ ] Add a historical state buffer for entities
- [ ] Add server rewind to a client's perceived time
- [ ] Add rewound hit detection for shooting
- [ ] Add per-client latency estimation for rewind
- [ ] Add rewound queries integrated with physics
- [ ] Add configurable rewind window limits
- [ ] Add favor-the-shooter and favor-the-target policies
- [ ] Add interpolation-aware rewind
- [ ] Add anti-abuse limits on rewind
- [ ] Add lag-compensation debugging visualization
- [ ] Add lag-compensation diagnostics
- [ ] Add lag-compensation tests

## Networked Physics

- [ ] Add replication of physics body state
- [ ] Add client prediction of physics
- [ ] Add server reconciliation of physics
- [ ] Add deterministic physics stepping for networked sim
- [ ] Add rollback and re-simulation of physics
- [ ] Add authority handling for physics objects
- [ ] Add sleeping and relevancy for networked bodies
- [ ] Add compressed physics-state encoding
- [ ] Add reconciliation smoothing for physics
- [ ] Add networked constraint and joint state
- [ ] Add networked destruction and break events
- [ ] Add networked-physics tests

## Dedicated Servers & Hosting

- [ ] Add a headless dedicated-server mode
- [ ] Add server startup, configuration, and shutdown
- [ ] Add multiple game instances per process
- [ ] Add sharding and world instancing across servers
- [ ] Add server discovery and registration
- [ ] Add load balancing across server instances
- [ ] Add container and orchestration friendliness
- [ ] Add horizontal scaling and autoscaling hooks
- [ ] Add server health checks and heartbeats
- [ ] Add graceful drain and instance handoff
- [ ] Add server-side logging and metrics export
- [ ] Add hot-config and remote administration
- [ ] Add crash recovery and instance restart
- [ ] Add server-side determinism for authoritative sim
- [ ] Add resource budgets per instance
- [ ] Add server-side anti-cheat validation hooks

## Listen Server, P2P & Host Migration

- [ ] Add a listen-server mode where the host is a player
- [ ] Add peer-to-peer connectivity
- [ ] Add host election and migration on host loss
- [ ] Add state transfer during host migration
- [ ] Add relay fallback when direct P2P fails
- [ ] Add mesh and star topologies
- [ ] Add authority handling in P2P sessions
- [ ] Add seamless client experience through migration
- [ ] Add P2P security and validation
- [ ] Add P2P and migration tests

## Matchmaking, Lobbies & Sessions

- [ ] Add lobby creation and joining
- [ ] Add rooms with capacity and settings
- [ ] Add ready-up and start flows
- [ ] Add party and group support
- [ ] Add skill-based matchmaking
- [ ] Add region and latency-based matching
- [ ] Add queue management and estimated wait
- [ ] Add backfill for in-progress matches
- [ ] Add dedicated-server allocation on match found
- [ ] Add reconnection to an active match
- [ ] Add invites and join-by-code
- [ ] Add presence and friends integration hooks
- [ ] Add session metadata and browsing
- [ ] Add matchmaking rules configuration
- [ ] Add matchmaking diagnostics
- [ ] Add matchmaking tests

## Security & Anti-Cheat

- [ ] Add encryption and authentication on all traffic
- [ ] Add server-side validation of all client inputs
- [ ] Add authority checks so clients cannot mutate others
- [ ] Add replay-attack protection with nonces and sequence checks
- [ ] Add rate limiting and flood protection
- [ ] Add packet sanity and bounds validation
- [ ] Add movement and physics sanity checks
- [ ] Add statistical cheat-detection hooks
- [ ] Add integrity checks and tamper detection hooks
- [ ] Add denial-of-service mitigation at the transport
- [ ] Add connection throttling and blacklisting
- [ ] Add secure token issuance and rotation
- [ ] Add audit logging of suspicious activity
- [ ] Add sandboxing of untrusted message handling
- [ ] Add privacy handling of player data
- [ ] Add security tests and fuzzing

## Web & Cross-Platform Transport

- [ ] Add a browser client over WebTransport
- [ ] Add a browser fallback over WebSocket
- [ ] Add a peer-to-peer browser path over web real-time transport
- [ ] Add a shared code path across native and web builds
- [ ] Add console and store platform networking backends
- [ ] Add platform certificate and trust handling
- [ ] Add cross-play between platforms
- [ ] Add platform-specific NAT and relay handling
- [ ] Add capability differences handled per platform
- [ ] Add cross-platform networking tests

## Voice & Media over Network

- [ ] Add a transport path for encoded voice
- [ ] Add positional voice synchronized with entities
- [ ] Add voice channels tied to sessions and teams
- [ ] Add jitter buffering for networked voice
- [ ] Add bandwidth sharing between voice and gameplay
- [ ] Add mute, block, and priority for voice
- [ ] Add optional media and data-stream transport
- [ ] Add voice-network diagnostics
- [ ] Add voice-network security
- [ ] Add voice-network tests

## Network Simulation, Debugging & Metrics

- [ ] Add a network simulator injecting latency, jitter, and loss
- [ ] Add reorder and duplication injection
- [ ] Add bandwidth throttling in the simulator
- [ ] Add a packet inspector and logger
- [ ] Add a replication visualizer per entity and client
- [ ] Add per-client bandwidth and RTT dashboards
- [ ] Add recording and replay of network sessions
- [ ] Add deterministic replay from recorded traffic
- [ ] Add a desync inspector for rollback and prediction
- [ ] Add server and client profiling of network cost
- [ ] Add machine-readable network metrics for CI
- [ ] Add live network overlays in play
- [ ] Add breadcrumb logging for connection issues
- [ ] Add reproduction capture for bug reports
- [ ] Add alerting hooks for server health
- [ ] Add simulation presets (mobile, wifi, congested)

## Testing & Validation

- [ ] Add transport reliability and ordering tests
- [ ] Add serialization round-trip and fuzz tests
- [ ] Add replication correctness tests
- [ ] Add entity-id mapping and remapping tests
- [ ] Add prediction and reconciliation tests
- [ ] Add rollback determinism and sync tests
- [ ] Add snapshot and delta correctness tests
- [ ] Add interest-management correctness tests
- [ ] Add bandwidth and rate-control tests
- [ ] Add lag-compensation tests
- [ ] Add networked-physics tests
- [ ] Add matchmaking and lobby tests
- [ ] Add security and anti-cheat tests
- [ ] Add cross-platform and web-client tests
- [ ] Add latency, loss, and reorder soak tests
- [ ] Add many-client scalability stress tests
- [ ] Add host-migration and reconnection tests
- [ ] Add FFI-boundary safety and leak tests
