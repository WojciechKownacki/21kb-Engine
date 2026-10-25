# Networking (kb_net)

`kb_net` is a separate static library (`sources/net`) that gives the engine a datagram transport,
authenticated sessions with reliable and unreliable channels, and a connection handshake. It links
`kb_engine` for cryptography (`kb::security`) and the budget and limit types in
`engine/network/NetworkBudget.hpp` and `NetworkSecurity.hpp`, and nothing in the engine links it, so
using it does not change any engine header. The existing `kb::network` types (objects, schemas, RPC
descriptors, prediction helpers) are unchanged; phase 1 provides the layer under them.

This is phase 1. It does not yet replicate entities, dispatch RPCs or predict movement: it moves
validated bytes between two peers. Those layers are built on `Session` messages.

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
