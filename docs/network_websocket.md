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
