# Networking (kb_net)

`kb_net` is a separate static library (`sources/net`) that gives the engine a datagram transport,
authenticated sessions with reliable and unreliable channels, and a connection handshake. It links
`kb_engine` for cryptography (`kb::security`) and the budget and limit types in
`engine/network/NetworkBudget.hpp` and `NetworkSecurity.hpp`, and nothing in the engine links it, so
using it does not change any engine header. The existing `kb::network` types (objects, schemas, RPC
descriptors, prediction helpers) are unchanged; phase 1 provides the layer under them.

Phase 1 is the transport, sessions and handshake. Phase 2 (`Replication.hpp`, `Rpc.hpp`) adds RPC
and state replication on top; see "Replication and RPC" below. Prediction.hpp adds client prediction and interpolation; there is no rollback of other entities.

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

## Scripts (Net.*)

Gameplay scripts reach the network through the `Net` module (Lua and Visual Graph), defined in
`engine/script/ScriptNetApi.*`. The engine does not link kb_net: it only knows the interface
`kb::network::INetworkScriptRuntime` (`engine/network/NetworkScriptBridge.hpp`). A game or a server
creates a `kb::net::ScriptNetworkRuntime` for its scene, which attaches itself; without one the scene is
offline and `Net.Mode()` says so.

| Function | What it does |
| --- | --- |
| `Net.Mode`, `Net.IsServer`, `Net.IsClient`, `Net.LocalPlayerId` | Which side this scene is and who the local player is. |
| `Net.RegisterRpc{ name, direction, args, reliable, owner, maxPerSecond }` | Declares an RPC (call it from `Created` or `Ready`). `direction` is `"toServer"` or `"toClients"`; `args` lists types such as `"entity,float(-1..1),string(32),uint,bool"`; `owner` is the index of an entity argument the caller must own. |
| `Net.Call{ rpc, target, n1.., s1.., b1.., e1.. }` | Calls it. The arguments go in numbered slots by kind (4 numbers, 2 strings, 2 bools, 2 entities), in declaration order; each is checked against its declared range or length before it leaves the script. `target` is a player id on a server, 0 for all. |
| `Net.Replicate{ entity }`, `Net.IsReplicated`, `Net.SetOwner{ entity, player }` | Server only. Mark an entity replicated, ask, and give it to a player. |

Events arrive as world events in the next script frame; the arguments are in `event.args`:
`OnNetConnected(player)`, `OnNetDisconnected(player, reason)`, `OnNetSpawned(entity, owner)` and
`OnNetDespawned(entity)` on a client, and `OnRpc_<name>(sender, n1.., s1.., ...)` for a call that arrived.
An `entity` on a client is an entity of the replica world and can be passed back as an argument of
`Net.Call`. A call whose entity argument the receiver does not have is dropped.

Order on both sides: create the `ScriptNetworkRuntime`, let the scripts start (they declare their RPCs and
mark entities), `Freeze` it into the `RpcRegistry`, create the `Host` or `Client` and the replication
object with that registry, and `Bind` the runtime to them. Both sides must declare the same RPCs, or the
hello refuses the client.

Tests: `ScriptNetApiTests.cpp` in `kb_engine_tests` (`script-net`: argument list parser, offline behaviour, declaration and
call validation against a fake session, a real Lua behaviour and event delivery) and `kb_net_script_tests` (a server
scene and two client scenes running one Lua script over a lossy simulated network: declaration, replication of
the hero, ownership, a move RPC, the reply, and the refusal of the player who does not own the hero).

## Dedicated server

`kb_dedicated_server` (built next to `kb_cli`) runs a project's startup scene without a window or a
renderer, steps it at a fixed rate, and replicates it over UDP.

    kb_dedicated_server --project <dir> --keys <file> [--scene <path>] [--port 27015] [--bind 0.0.0.0]
        [--max-clients 16] [--tick-rate 30] [--idle-timeout 30] [--empty-timeout <s>] [--max-seconds <s>]
        [--replicate all|none|prefix:<name>] [--world-extent 1000] [--stop-file <path>] [--port-file <path>]
        [--status-file <path>] [--state-file <path.21kbscene>] [--json] [--quiet]

* **Keys.** The keys file lists one `<client id> <64 hex digits>` per line (`#` starts a comment). Only
  those clients can connect; the handshake is the one above, so a wrong key or an unknown id gets no session.
* **What is replicated.** The local transform (`SceneTransformCodec`: position over +-`world-extent` with 16
  bits, rotation, scale; 20 bytes) of the entities chosen by `--replicate`: all that have a transform, those
  whose name starts with a prefix, or none. An entity is judged once, when it is first seen. The server
  also defines the standard RPCs `Ping`/`Pong` and `Say`/`Said` (`DefineStandardRpcs`); a client builds the
  same registries (`MakeSceneReplicationRegistry`, `DefineStandardRpcs`) and uses `ReplicationClient`.
* **Scripts.** The server's scripts see `Net.*`: they can declare RPCs, call clients, mark entities with `Net.Replicate`, and react to
  `OnNetConnected` and `OnRpc_<name>` (see Scripts above). The test project's script greets each player by id and answers an `Echo` RPC.
* **Loop.** The scene (and its scripts) advance by a fixed step of `1 / tick-rate`; replication runs at the
  same rate; the loop catches up at most five steps behind and then drops the backlog.
* **Limits.** `--max-clients` (a further client is not accepted), `--idle-timeout` (a client silent for this
  long is dropped), `--empty-timeout` and `--max-seconds` end the run, and the per-session limits of kb_net apply.
* **Stopping.** SIGINT/SIGTERM (Ctrl+C or Ctrl+Break on Windows) or the stop file: the server tells the
  clients, closes and exits 0. Exit code 1: it could not start (missing project, scene, keys, port in use)
  or failed; 2: bad command line. Log lines go to stdout, or to stderr with `--json`, which prints one JSON
  object (`21kb.dedicated-server/v1`: exit reason, ticks, clients, host and replication counters) on stdout at
  the end; `--status-file` holds the same object, refreshed about once a second.
* **Scene persistence.** `--state-file` loads an existing scene checkpoint instead of the startup scene and saves the current scene
  on clean shutdown. A missing checkpoint starts from the project scene; an unreadable checkpoint refuses startup without replacing it.
  The scene file contains entities, serialized components and asset references. It does not preserve Lua coroutine stacks, network
  connections or physics solver state. Scripts initialize again after a restart. A save failure exits 1 with `state-save-error`;
  status JSON reports `state_loaded` and `state_saved`. The scene writer uses atomic replacement for the scene payload.
* **Tested.** `kb_dedicated_server_tests` starts the real process and talks to it over real UDP on 127.0.0.1:
  handshake, replication of the scene's entities, RPC both ways and a broadcast, a wrong key and an unknown id,
  600 hostile datagrams, a client dropped by the idle timeout, the client limit, `--max-seconds` and
  `--empty-timeout`, a clean stop by file with exit code 0 and the final JSON, and the command line errors.
  This is the remote process proof the transport needed (A145).

Not done in the server: it replicates the local transform only (no hierarchy, no other component), has no
player spawning or authority rules of its own (scripts and RPCs would add them), and has not been run on
Linux.

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
