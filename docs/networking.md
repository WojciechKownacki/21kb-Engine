# Networking (kb_net)

`kb_net` is a separate static library (`sources/net`) that gives the engine a datagram transport,
authenticated sessions with reliable and unreliable channels, and a connection handshake. It links
`kb_engine` for cryptography (`kb::security`) and the budget and limit types in
`engine/network/NetworkBudget.hpp` and `NetworkSecurity.hpp`, and nothing in the engine links it, so
using it does not change any engine header. The existing `kb::network` types (objects, schemas, RPC
descriptors, prediction helpers) are unchanged; phase 1 provides the layer under them.

Phase 1 is the transport, sessions and handshake. Phase 2 (`Replication.hpp`, `Rpc.hpp`) adds RPC
and state replication on top; see "Replication and RPC" below. Prediction and rollback are not done.

## Layers

| Piece | What it is |
| --- | --- |
| `IDatagramTransport` | Non-blocking send and receive of datagrams up to 1200 bytes (`kMaxDatagramBytes`). IPv4 only. |
| `CreateUdpTransport` | UDP over Winsock or BSD sockets. Not available in the browser build (returns `nullptr` with a message). |
| `LoopbackNetwork` | In-process network for tests. Time moves only through `Advance`. Loss, reordering, latency and jitter come from the engine's seeded `NetworkSimulationConfig`; duplication is added. The same seed gives the same run. |
| `Session` | One authenticated connection as a state machine. It never touches a socket; the caller passes the clock. |
| `Host`, `Client` | The handshake, a table of sessions, events (`Connected`, `Message`, `Disconnected`) and limits. |

## Session behaviour

* Every datagram is sealed with XChaCha20-Poly1305 under a key used for one direction only. The
  packet number is the nonce and the 12 byte header is the associated data, so a changed bit
  anywhere fails authentication.
* A 64 packet window rejects replayed, duplicated and too old datagrams. A packet that fails
  authentication does not use up its number.
* Reliable ordered channels: acknowledgements (latest packet plus a 32 packet bitmap), retransmission
  with an exponentially backed off timeout derived from a smoothed round trip time, fragmentation of
  messages up to `maxMessageBytes` (default 16 KiB, at most 255 fragments) and in-order delivery.
* Unreliable sequenced channels: a message may be lost, and one older than the newest delivered is
  dropped.
* Back pressure instead of loss: a datagram whose reliable data cannot be kept (reassembly buffer or
  window full) is refused as a whole and not acknowledged, so the sender retransmits it. A full
  application inbox holds delivered messages back the same way. `Send` returns `Busy` when the
  send queue (`budget.maxQueuedBytes`) or the 256 message window is full.
* Every length, count, index and channel read from the wire is checked; a bad datagram is counted
  (`SessionStats`, `HostStats`) and changes nothing.

## Handshake

Each client has a 32 byte secret that the host also holds (`IKeyProvider`). The key is never sent.

1. `Connect` (client id, client salt, padded to at least 128 bytes).
2. `Challenge` (client salt, server salt, MAC): the server salt is a cookie derived from a host secret
   and the client address, and the host stores nothing. The Challenge is smaller than the Connect, so
   the host cannot be used to amplify traffic to a spoofed address.
3. `Response` (ids, salts, MAC): accepted only from the address that received the Challenge and only
   with a valid MAC. The client verifies the Challenge MAC first, so it also knows the host holds the key.

Both sides then derive the two direction keys with HKDF-SHA-512 from the key and both salts. The
first sealed packet from the host confirms the connection. A repeated `Response` from the same
address does not create a second session; the same client id arriving from another address replaces
the old connection.

## Limits

| Limit | Default | Where |
| --- | --- | --- |
| Datagram | 1200 bytes | `kMaxDatagramBytes`, `budget.packetBytes` |
| Reliable message | 16 KiB | `SessionConfig::maxMessageBytes` |
| Unreliable message | 1000 bytes | `SessionConfig::maxUnreliableBytes` |
| Send queue and reassembly buffer | 64 KiB | `budget.maxQueuedBytes` |
| Datagrams handled per peer per `Update` | 64 | `limits.maxMessagesPerTick` |
| Datagrams read per `Update` | 1024 | `HostConfig::maxDatagramsPerUpdate` |
| Clients | 64 | `HostConfig::maxClients` |
| Inactivity timeout | 10 s | `SessionConfig::timeoutMilliseconds` |

## Replication and RPC

The server is authoritative. `ReplicationServer` takes a `Host` and an ECS `World`; `ReplicationClient`
takes a `Client` and the client's own `World`. The application passes every `NetEvent` to
`HandleEvent`, calls `ReplicationServer::Tick` at its tick rate and `ReplicationClient::Flush` once
per tick.

* **What replicates.** `Replicate(entity, owner)` gives an entity a network id (a `std::uint32_t`,
  never reused in a run). Only the components registered in a `ReplicationRegistry` travel, each
  through a `StructCodec<T>` that lists its float, unsigned and bool members. Floats are quantized
  (`Float(&T::x, min, max, bits)`, 1 to 24 bits, clamped to the range; a position over +-500 with 16
  bits costs 2 bytes and has a step of 0.015). Other components never leave the server. Up to 16 components.
* **Why not the ECS snapshot.** `WorldSnapshot` and the chunked deltas save and restore a whole world
  on one machine: raw component memory, local entity ids, every component, no per-client view, no
  quantization. They cannot be restricted to what a client may see, so the network layer has its own
  codecs and baselines.
* **Spawn and despawn** are reliable and ordered. A Spawn carries the whole state of the entity. A
  client creates the entity only after every component in the message has been checked, and destroys
  it on Despawn; it never holds an entity the server no longer shows it.
* **State updates** are unreliable. For each client the server remembers what the client has
  acknowledged and sends, for each entity, every replicated component that differs from that
  baseline. So a lost, duplicated or reordered update is repaired by the next one, nothing is
  retransmitted, and a component changes on the wire only when its quantized value changes. The client
  acknowledges a state message only when every entry in it was applied to an entity it has and was not
  older than what that entity shows, because the server treats an acknowledged message as delivered
  state. Updates are bounded by `maxUpdateBytes` per message and `maxBytesPerClientPerTick` per
  client; a big change is spread over several ticks.
* **Relevance.** `IRelevance` decides, per client and entity, whether the client may see it; an
  entity that stops being relevant is despawned on that client only. `RadiusRelevance` is a distance
  rule with a hysteresis band so that an entity on the edge does not flicker.
* **Ownership.** Each replicated entity has an owner (client id, 0 for the server). `SetOwner`
  tells the clients that see it. Ownership is what an RPC's permission check uses.
* **Late join and reconnect.** A client that connects (again) gets a Hello, answers with the
  fingerprints of its component and RPC registries, and only after a match does it receive Spawns for
  everything it may see. A client built with a different layout is refused and gets no state.
* **RPC.** `RpcRegistry` declares each call: name, direction (client to server or server to client),
  reliable or unreliable, typed arguments with ranges (bool, int32, uint32, float, string with a
  maximum length, network id), an optional owner argument, and a rate limit. The receiver rejects, and
  counts, a call that has an unknown id, the wrong direction, the wrong argument count, type, range or
  length, trailing or missing bytes, a caller that does not own the entity named by the owner argument,
  or too many calls per second; a handler never sees such a call, and a handler that throws is contained.
  A client has no way to write server state: the server accepts from a client only the acknowledgement
  and RPC messages, and everything else is counted as malformed.

Tests: `kb_net_replication_tests` covers the codecs, the RPC wire format and its refusals, convergence
on a perfect network (movement, component add/remove, ownership, despawn, late join, reconnect), on a
network with 25% loss, 20% reordering and 10% duplication (24 seeds, with spawn and despawn churn),
RPC in both directions with permissions, a client that tries to heal an entity it does not own, forged
state messages, a registry mismatch, relevance with hysteresis, size and bandwidth limits, and 120000
random and mutated messages into a client replica and into the server.

## Prediction and interpolation

`Prediction.hpp` has two header-only helpers for state that the game declares and steps deterministically.

* `ClientPrediction<State, Input>`: `Predict(input)` applies an input to the local state at once and
  returns its sequence number (the client sends it to the server, reliably and in order, for instance as
  an RPC with an owner argument); the server applies inputs in order and replicates its state together
  with the sequence number of the last input it applied; `Reconcile(authoritative, lastProcessed)`
  forgets the inputs up to there, replays the rest on top of the server's state and reports how far the
  prediction was off. The history is bounded (`Predict` refuses past `maxPending`), and an acknowledgement
  of an input that was never sent is refused.
* `SnapshotInterpolator<State>`: shows a remote entity a fixed delay in the past between two snapshots;
  it holds the newest snapshot instead of extrapolating, and keeps a bounded history.

Contract: `Step` must be the same pure function on both sides. A server that also does something the
client cannot know (a wall, another player) makes the prediction wrong by exactly that, which
`Reconcile` measures and corrects (the test shows 13 corrections of up to 1.8 units against a wall only the
server has, and convergence once the inputs stop). A provider that is not deterministic, such as a
physics engine stepped differently on the two sides, cannot be predicted this way. There is no rollback of
other entities (the server is always the only truth), and no lag compensation for hits.

## Not done

* Interest management beyond a per-client predicate (no spatial grid); every tick evaluates every
  entity for every client.
* Delta compression of components within an entity (an entity sends its changed components whole).

## Not done in phase 1

* Congestion control. The sender retransmits on a timer and has no rate limit beyond the queue
  limits; a slow link is not protected from a fast sender.
* Forward secrecy and key distribution. The client key is long lived and must reach both sides over
  a channel the application already trusts. A leaked key lets its holder impersonate that client.
* IPv6, NAT rebinding (a session is tied to the source address), path MTU discovery.
* A browser transport (WebSocket or WebRTC). The browser build compiles without UDP.
* Entity replication, RPC dispatch, prediction and interest management (A146, A147 and later).

## Tests

`kb_net_tests` (ctest name `kb_net_tests`) covers wire reading, configuration, the loopback network,
sessions over direct calls, fragmentation, the replay window, tampered packets (every byte of a
packet changed in turn), back pressure without loss, queue limits, the handshake (wrong key,
unknown client, invented MAC, replayed Response from another address, short Connect), a flood from
one peer, 20000 random and mutated datagrams into a host and 50000 into a session, disconnect and
timeout, host capacity, a 30% loss, 15% duplication, 20% reordering network with 200 reliable
messages each way, two clients on that network with one of them reconnecting from another address
(which prints a report of loss, round trip time, retransmissions and rejected packets), and a real UDP
round trip on 127.0.0.1.
