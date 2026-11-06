#pragma once

#include "kb/net/Transport.hpp"

namespace kb::net {

inline constexpr const char* kDatagramWebSocketProtocol = "21kb.datagram.v1";

// One binary WebSocket message carries one session datagram. The underlying
// stream is ordered, so packet loss can delay subsequent unreliable messages.
// Call from the browser's event-loop thread and yield regularly. Connection is
// asynchronous: Send returns WouldBlock until the subprotocol is negotiated.
// HTTPS pages require wss://; the browser owns TLS and certificate validation.
// `peer` is the logical endpoint supplied to Client, not a DNS lookup result.
[[nodiscard]] std::unique_ptr<IDatagramTransport> CreateBrowserWebSocketTransport(
    const std::string& url, Endpoint peer, std::string& error);

struct WebSocketServerOptions {
    Endpoint udpBind{};
    std::uint16_t webSocketPort = 0U; // zero selects an available TCP port
    std::size_t maxClients = 64U;
    // Empty accepts any Origin; session authentication still applies.
    std::string allowedOrigin;
};

// Native UDP and WebSocket peers share one Host and its authenticated sessions.
// The gateway serves /21kb over ws://; deployments may terminate wss:// at a
// reverse proxy. Endpoints in 0/8 are reserved for connection identities.
[[nodiscard]] std::unique_ptr<IDatagramTransport> CreateWebSocketServerTransport(
    const WebSocketServerOptions& options, std::uint16_t& webSocketPort, std::string& error);

} // namespace kb::net
