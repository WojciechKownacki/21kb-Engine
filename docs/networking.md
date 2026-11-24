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
* `NextDueMs` keeps idle sessions out of packet construction. `BuildPacket(now, out)` reuses the
  caller's reserved output buffer; packet history, decrypted bytes and parsed records have fixed
  capacity. `WaitReadable` wakes the server on socket traffic or at the next tick deadline.
  `KB_NET_PROFILE=1` enables cumulative visibility, candidate, ordering and encoding timings in the
  server metrics CSV; profiling is disabled by default.

## Handshake

Each client has a 32 byte secret that the host also holds (`IKeyProvider`). The key is never sent.
Do not use a password-derived value as this transport secret: a recorded handshake MAC verifies
password guesses offline. Password admission requires authentication bound to the DH shared secret.

For admission without a key table, set `HostConfig::admission` and `ClientConfig::admission` to
`Admission::JoinPassword` with the same `joinPassword`. The host and client overloads without
keys use this mode. `Host::HostSalt()` exposes the random eight-byte host salt for discovery.
CPACE-X25519-SHA512 follows draft-irtf-cfrg-cpace-21 (length-prefixed generator input, zero padding,
and initiator/responder transcript). The host salt is CI; the client salt and address cookie form sid.
The client and server ephemeral public keys are authenticated as associated data in ISK.
The underlying Elligator map accepts 254-bit representatives; canonicalizing the 255-bit draft
input and selecting its sign preserves the map, which depends only on its square.

Connect remains stateless and its Challenge is smaller. Only a cookie-verified Response starts a
bounded, expiring PAKE exchange. `PasswordChallenge` returns the server's blinded point and fresh
ephemeral key; `PasswordFinish` proves the client's ISK. Password rejection occurs online at the
host, reports `PasswordRejected`, and never emits Accept. New exchanges consume the existing
per-address Response allowance and report `PasswordRateLimited`; bucket exhaustion refuses
password exchanges instead of bypassing this allowance. Retransmissions reuse the same exchange
without extending its expiry or creating another session.

ISK supplies the Accept confirmation key and joins the independent ephemeral X25519 shared
secret in session HKDF. Encrypted Accept adds a random, unique 64-bit `assignedId` and an
authenticated-guest flag. Read these through `Client::AssignedId()` and `GuestAuthenticated()`.
Claimed client ids cannot replace other guests. Ids zero and one are reserved in server key files.
An empty password explicitly means encrypted connections **without guest authentication**;
Accept marks this state and `UnauthenticatedGuests` reports it through the diagnostic log.
The passive dictionary regression exercises 1000 guesses, including the correct password, without
an online oracle; it detects deterministic password tags, not a proof of computational indistinguishability.

1. `Connect` contains client id, client salt, release (U32), content hash (U64), and a fresh 32-byte
   client ephemeral public key, padded to 128 bytes.
2. `Challenge` contains the client salt, address cookie, and MAC; the host stores no pending state.
3. `Response` repeats the client ephemeral public key; its MAC covers the key and both salts.
4. `Accept` carries a random 24-byte nonce and an authenticated encrypted 32-byte server random,
   server release, content hash, and server ephemeral public key, followed by a 16-byte tag.
   The header and nonce are authenticated.

Protocol version 2 uses HKDF-SHA-512 with the client and server salts and the label
`21kb-net-accept-v2` for the Accept key. Session keys use those salts plus the fresh server random
and the label `21kb-net-session-v2`, with the client secret and ephemeral X25519 shared secret as
input keying material. The client creates its session only after authenticating Accept.
A duplicate Response during a live session repeats the exact cached Accept without resetting packet
numbers. After teardown, a valid replay creates fresh keys: old session packets fail authentication.
Peers that send no authenticated data expire after `confirmMilliseconds` (default 5000 ms), even
when Responses are repeated.

A 25-byte Reject echoes the client salt and names ProtocolVersion, OutdatedClient, ContentMismatch,
or ServerFull, together with the server identity. A release below `minClientRelease` reports
`VersionMismatch` to the client, with `OutdatedClient` retained in `Client::RejectInfo()`.
Content hashes are checked when both are nonzero; a zero release and hash explicitly report
`VersionUnchecked` through diagnostics. Rejects are unauthenticated and provide a refusal reason,
not proof of server identity. Invalid MACs and cookies receive no refusal response.

The dedicated server verifies `release.kbmanifest` beside the project using its release trust anchor
when present; `--release` and `--content-version` override its identity. Scene clients obtain identity
from the current verified release unless explicitly configured. The content hash is the first eight
SHA-512 bytes of the content version, decoded little endian.

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

* **What replicates.** `Replicate(entity, owner)` reports whether an entity was registered. `NetworkIdOf(entity)` returns its 64-bit id, with a 32-bit slot index and 32-bit generation. Only the components registered in a `ReplicationRegistry` travel, each
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
  state. Updates are bounded by `maxUpdateBytes` per message and a negotiated byte rate per client,
  with a burst of two ticks. Clients request `downlinkBytesPerSecond`; the server clamps the request
  to its configured limits (`--max-client-rate` controls the upper limit). Near, middle and far
  objects are eligible every one, two or four ticks; accumulated priority prevents starvation.
  Sustained loss or excessive RTT reduces the rate; clean traffic gradually restores it. Five seconds
  without acknowledgements stop state traffic until receiver acknowledgements resume.
* **Declarations.** `Config/Replication.json` is shared by servers, clients, tools and cooked packages.
  `BuildReplicationRegistry` adds the three transform groups and the declared native fields and
  script variables. No file means transform groups only. Field ranges, bit counts and audiences
  participate in the registry fingerprint, so different declarations refuse the connection.
  Audiences are `Everyone`, `Owner`, `Others` and `InitialOnly`; changing ownership revokes private fields.
  `Net.Replicate` prepares the variable component. The server writes `Net.SetVar(entity, "score", 12)`;
  allowed clients read `Net.GetVar(replica, "score")` and receive `OnNetVarChanged(entity, name, value)`
  once per changed value. Variables support Float, Int and Bool (0 or 1).
* **Relevance.** A spatial grid selects nearby objects in the configured plane (XZ by default, XY for planar scenes).
  `--visibility-radius` enables the radius; zero sends every object and warns for large populations.
  Owned objects and objects marked with `Net.Replicate(..., mode="global")` are always visible.
  A client without an owned object with a position sees only its own and global objects.
  Objects enter at the radius and leave beyond 1.15 times the radius.
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

The scene client (`SceneClient`, which the game and `kb_cli net check` use) applies `SnapshotInterpolator` to every replica's transform: it records each
state message on a clock of the server's ticks (the tick period is read from how fast they arrive and followed gently, so a datagram that comes a frame late does
not bunch two snapshots together) and shows the replica 0.1 s in the past, between the two snapshots around that time, so a script that reads the replica every
frame sees steady motion although the server sends 30 states a second. A replica that stops is held at its last snapshot; nothing is extrapolated. `kb_cli net check
--trace <name>` lists where each player's replica of the objects called that is at every picture (the players draw twice per 30 Hz step), and
`KbCliNetTests.cpp` (`RunSmoothnessTest`) fails when a walking character's steps are uneven. `ClientPrediction` is still not used by the scene client: a
player's own character follows the server.

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

A client can call the server once `Net.Mode().connected` is true (the server has agreed on the declarations); `OnNetConnected` comes a moment earlier, when the session opens.
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

## Trying a game of several players: kb_cli net

`kb_cli net info` prints what a script can use, read from the engine's own registries (the `Net.*` functions, the network events, the replicated components, the standard calls and
the limits). `kb_cli net setup --project <dir> --players 4 --address <IPv4>` writes `Saved/Net/keys.txt` for the server and a session file for each player (and refuses a folder
inside `Assets/`, which a build packs into the game). `kb_cli net check --project <dir> [--clients 2] [--frames 300] [--press 2:W@30+60] [--click 1:Canvas/Start@40] [--loss 10]`
loads the project as the server and as each player in one process over the simulated network of `kb_net` (`ProjectScene`, `SceneClient`, `ReplicationServer` and the script bridge,
the same code the dedicated server and the game run), steps them at 30 frames a second and prints who connected, what was replicated and where each player sees it, the scripts' `Log`
lines and `warning:` lines for the usual mistakes (nothing replicated, an object moved on the server that is not replicated, a call refused for lack of an owner or a range, a player
refused because its RPC declarations differ, a replica that does not match the server). The server of `net check` leaves replication to the scripts, like
`kb_dedicated_server --replicate none`. The recipes `multiplayer-lan`, `multiplayer-online`, `net-players`, `net-score`, `net-lobby`, `net-chat` and `net-object` are built on it.

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
  bits, rotation in 4 bytes, and scale; three independent groups of 6/4/6 bytes) of the entities chosen by `--replicate`: all that have a transform, those
  whose name starts with a prefix, or none. An entity is judged once, when it is first seen. The server
  also defines the standard RPCs `Ping`/`Pong` and `Say`/`Said` (`DefineStandardRpcs`); a client builds the
  same registries (`BuildReplicationRegistry`, `DefineStandardRpcs`) and uses `ReplicationClient`.
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
* **Socket buffers.** The server asks for a 1 MiB `SO_RCVBUF`/`SO_SNDBUF`, because a burst of 300 clients
  joining in one tick overflows the 208 KiB default Linux buffer. The startup log prints the sizes the
  socket actually got back from `getsockopt`; a smaller value is logged and reported as the
  `SocketBufferSmall` diagnostic, which on Linux names `net.core.rmem_max` and `net.core.wmem_max`.
* **Linux.** Configure with `-DKB_BUILD_EDITOR=OFF -DKB_BUILD_RENDERER=OFF -DBUILD_TESTING=ON`; `kb_dedicated_server`,
  `kb_net_bot` and the network test executables build with GCC. `scripts/net_linux_smoke.py --server <kb_dedicated_server>
  --bot <kb_net_bot>` makes a sample project and keys, runs the server and three bots over real UDP, checks
  `clients_peak`, the replicated object count and the diagnostics, sends SIGTERM and checks exit code 0 within
  three seconds and the saved scene checkpoint. ctest registers it as `kb_net_linux_smoke` (labels `network;process`,
  Linux only); the CI job `Linux dedicated server` builds the targets, runs `ctest -L network` and then the script.
  On a two-core Linux host in Release, a 50-bot run on a 1000-object scene kept 50 clients for 20 s: the server
  wrote its port file 0.03 s after start, peaked at 41 MiB resident and used about a third of one core at 30 ticks
  per second; the socket reported 2 MiB buffers for the 1 MiB request.
* **Tested.** `kb_dedicated_server_tests` starts the real process and talks to it over real UDP on 127.0.0.1:
  handshake, replication of the scene's entities, RPC both ways and a broadcast, a wrong key and an unknown id,
  600 hostile datagrams, a client dropped by the idle timeout, the client limit, `--max-seconds` and
  `--empty-timeout`, a clean stop by file with exit code 0 and the final JSON, and the command line errors.
  This is the remote process proof the transport needed (A145).

Not done in the server: it replicates the local transform only (no hierarchy, no other component), and has no
player spawning or authority rules of its own (scripts and RPCs would add them).

## Not done

* Interest management beyond a per-client predicate (no spatial grid); every tick evaluates every
  entity for every client.
* Delta compression of components within an entity (an entity sends its changed components whole).

## Not done in phase 1

* Congestion control. The sender retransmits on a timer and has no rate limit beyond the queue
  limits; a slow link is not protected from a fast sender.
* Key distribution. The client key is long lived and must reach both sides over
  a channel the application already trusts. A leaked key lets its holder impersonate that client.
* IPv6, NAT rebinding (a session is tied to the source address), path MTU discovery.
* Raw browser UDP is unavailable. The supported WebSocket adapter and dedicated-server listener are described in [Browser session transport](network_websocket.md).
* Entity replication, RPC dispatch, prediction and interest management (A146, A147 and later).

## Tests

`kb_net_tests` (ctest name `kb_net_tests`) covers wire reading, configuration, the loopback network,
sessions over direct calls, fragmentation, the replay window, tampered packets (every byte of a
packet changed in turn), back pressure without loss, queue limits, the handshake (wrong key,
unknown client, invented MAC, replayed Response from another address, short Connect), a flood from
one peer, 20000 random and mutated datagrams into a host and 50000 into a session, disconnect and
timeout, host capacity, a 30% loss, 15% duplication, 20% reordering network with 200 reliable
messages each way, two clients on that network with one of them reconnecting from another address
(which prints a report of loss, round trip time, retransmissions and rejected packets), a real UDP
round trip on 127.0.0.1, and the socket buffer sizes read back from a socket that asks for 1 MiB and the
warning produced when the kernel caps them.

## Dedicated-server distribution

`scripts/package_server.py --binary <kb_dedicated_server> --output <new-directory>`
creates a runtime distribution containing the executable, license notices,
SBOM and SHA-256 file inventory. Optional `--runtime-library` arguments add
explicit native shared libraries. Existing output directories are refused.

Use `--process-tests <kb_dedicated_server_tests>` to run the real server suite
against the staged executable before publishing the directory. This checks
multiple clients, replication and script RPC, signed login tokens, persisted
scene checkpoints, invalid checkpoint handling, metrics and clean shutdown
from the relocated runtime. Project content and operator credentials remain
separate deployment inputs passed through `--project` and `--keys`; they are
never collected from the build tree by the runtime packager. Install the
platform runtime libraries required by the selected native binary.

Session keys combine the client key with a fresh ephemeral X25519 shared secret using HKDF-SHA-512. The ephemeral public key is authenticated in the handshake, and private ephemeral keys are wiped after agreement. A later client-key leak does not decrypt recorded sessions.
