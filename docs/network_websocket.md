# Browser session transport

The dedicated server can serve native UDP clients and browser WebSocket clients
through the same session, authorization and replication layers. Enable its TCP
listener with `--websocket-port 27016`; zero selects an available port. The
status JSON reports `websocket_port`. `--websocket-origin https://example.com`
restricts browser upgrades to an exact Origin. An omitted restriction accepts
any Origin; it does not bypass session keys or optional signed login tokens.

Connect to `/21kb` with subprotocol `21kb.datagram.v1`. Each binary WebSocket
message contains one session datagram, up to 1200 bytes. Text messages and
oversized or cumulatively oversized fragmented messages close the connection.
UDP and WebSocket peers share the server's client limit. The transport reserves
IPv4 endpoint identities in `0/8` for WebSocket connections.

`CreateBrowserWebSocketTransport` creates a nonblocking browser adapter. Supply
the same logical endpoint to the adapter and the session client. Pump the client
on the browser event-loop thread and yield between updates. Sends return
`WouldBlock` during connection setup or bounded queue backpressure. A failed or
closed connection requires a new transport and session handshake.

The browser owns TLS and certificate validation. HTTPS pages must use `wss://`.
The native listener serves plain `ws://`; production deployments can terminate
TLS at a reverse proxy that preserves the path, subprotocol and Origin.

WebSocket uses an ordered stream. Loss can delay later unreliable messages;
this transport does not reproduce UDP's independent packet delivery. The
browser receive queue holds 256 datagrams; native queues hold 256 outgoing
datagrams per peer and 4096 globally in each direction. Overflow closes the
receiving connection or returns send backpressure. Browser credentials should
be provisioned at runtime and kept out of URLs, cooked assets and logs.

Native support uses the pinned libwebsockets dependency and can be disabled with
`KB_NET_WEBSOCKET_SERVER=OFF`. Browser builds use Emscripten's WebSocket bridge.

## Packaged Web runtime

Set `globalThis.kbNetwork` before the exported host starts. Supply `url`, a
string `clientId`, and a 64-digit hexadecimal session `key`. Optional fields
are `token`, `worldExtent` (default 1000) and `reconnectAttempts` (default 3,
maximum 10). Both peers must use the same extent and RPC declarations.
The host consumes and removes this object; it does not persist or log it.
Deliver credentials through the application login flow, separate from the export.

The runtime attaches `Net.*` before scripts start, freezes RPC declarations
after their first update, and replicates transforms into the scene. Scripts
receive connection, spawn, despawn and RPC events. A signed-token session enables
client calls only after the server grants play scope. A transport disconnect
clears replicas and retries after one second, up to the configured lifetime
attempt limit; rejected authorization is not retried automatically.

## WebRTC data channels

WebRTC is a third transport for the same session, authorization and replication
layers. A link is one peer connection with two data channels: a reliable ordered
one for whole messages up to `WebRtcConfig::maxMessageBytes` (256 KiB by default,
which every browser accepts), and an unreliable unordered one that carries the
session datagrams. The session layer adds its own reliability and fragmentation,
so it runs over WebRTC unchanged and, unlike WebSocket, a lost packet never
delays the ones behind it. UDP, WebSocket and WebRTC peers can share one `Host`;
WebRTC peers take identities in the upper half of the `0/8` range.

Setup. Peers first exchange a session description and ICE candidates over a
signaling channel, then talk directly and the signaling channel closes.

- Server: start the dedicated server with `--websocket-port 27016 --webrtc`.
  The same listener then also serves signaling at `/21kb-rtc` with subprotocol
  `21kb.signal.v1` (text messages `offer`, `answer` and `candidate`, each a
  first line naming the kind followed by its body). `--websocket-origin` applies
  to it as well. In code, set `WebSocketServerOptions::signaling` to a shared
  `SignalingListener` and wrap the listener's transport with
  `CreateWebRtcServerTransport`.
- Client: open `CreateWebSocketSignalingClient("ws://host:27016/21kb-rtc")` and
  pass it to `CreateWebRtcClientTransport` together with the logical server
  endpoint; use that transport with `kb::net::Client` as with any other.
  Browser builds do the same with the page's RTCPeerConnection and can only
  offer. The signaling channel is any `ISignalingChannel`, so a game can also
  carry the messages over its own service; `CreateSignalingPair` connects two
  peers inside one process.
- Close a link by destroying its transport. The other side sees the link close
  at once, or after `WebRtcConfig::timeoutMilliseconds` of silence (10 s by
  default); empty messages are keep-alives and are never delivered.

ICE servers. `WebRtcConfig::iceServers` (and `--ice-servers` on the server, a
comma separated list) take `stun:host:3478` entries, which let a peer behind a
NAT learn its public address, and `turn:user:password@host:3478` entries, which
relay traffic when no direct path exists. With none, only host candidates are
used, which is enough on one machine or one network. `bindAddress` restricts the
native side to one local address. Browsers reach the server over the public
Internet only when the server's address is reachable, directly or through STUN
or TURN; HTTPS pages must use `wss://` for signaling, which a reverse proxy can
terminate as for WebSocket.

Native support uses the pinned libdatachannel dependency with Mbed TLS and can be
disabled with `KB_NET_WEBRTC=OFF`; the archives are fetched once per build tree.
