#pragma once

#include "kb/net/Signaling.hpp"
#include "kb/net/Transport.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

// WebRTC data channels as a datagram transport. A link is one peer connection with two data channels:
// a reliable ordered one, whose messages arrive whole and in order up to WebRtcConfig::maxMessageBytes,
// and an unreliable unordered one for datagrams. The session layer (Session.hpp, Host.hpp) and
// everything above it run unchanged over the unreliable channel, because it adds its own reliability.
//
// The offerer (a browser or native client) and the answerer (the server) first exchange session
// descriptions and ICE candidates over an ISignalingChannel, then talk directly. Native builds use a
// bundled WebRTC library; the browser build uses the browser's RTCPeerConnection and can only offer.
// Calls never block, and a link must be polled with Update from one thread.
namespace kb::net {

struct WebRtcConfig {
    // "stun:host:port", or "turn:user:pass@host:port" for a relay. Empty: host candidates only, which
    // is enough on one machine or one network.
    std::vector<std::string> iceServers;
    std::string bindAddress; // native only: the local address ICE binds to; empty uses every interface
    std::uint32_t connectTimeoutMilliseconds = 15'000U; // from creation until both channels are open
    std::uint32_t timeoutMilliseconds = 10'000U;        // nothing received for this long closes an open link
    std::size_t maxMessageBytes = 256U * 1024U;         // the largest message; 256 KiB is accepted by every browser
};

enum class WebRtcChannel : std::uint8_t { Reliable, Unreliable };
enum class WebRtcState : std::uint8_t { Connecting, Open, Closed };
enum class WebRtcCloseReason : std::uint8_t {
    None,
    Local,          // Close was called
    Remote,         // the peer closed or its connection dropped
    Timeout,        // nothing arrived for WebRtcConfig::timeoutMilliseconds
    ConnectTimeout, // the channels did not open in time
    Error,          // the signaling messages, the channels or the library failed
};

class IWebRtcLink {
public:
    IWebRtcLink() = default;
    IWebRtcLink(const IWebRtcLink&) = delete;
    IWebRtcLink& operator=(const IWebRtcLink&) = delete;
    virtual ~IWebRtcLink() = default;

    // Applies received signaling messages, sends keep-alives and checks the timeouts.
    virtual void Update() = 0;
    [[nodiscard]] virtual WebRtcState State() const = 0;
    [[nodiscard]] virtual WebRtcCloseReason CloseReason() const = 0;
    // Ok: queued. WouldBlock: not open yet, or too much is waiting. TooLarge: empty or over the limit.
    // Error: the link is closed.
    [[nodiscard]] virtual TransportStatus Send(WebRtcChannel channel, std::span<const std::uint8_t> message) = 0;
    // Messages arrive whole. Empty messages are keep-alives and are never delivered.
    [[nodiscard]] virtual bool Receive(WebRtcChannel channel, std::vector<std::uint8_t>& message) = 0;
    virtual void Close() = 0;
};

// The side that sends the offer. Returns nullptr and fills `error` when the configuration or the
// platform is not supported.
[[nodiscard]] std::unique_ptr<IWebRtcLink> CreateWebRtcOfferer(
    std::unique_ptr<ISignalingChannel> signaling, const WebRtcConfig& config, std::string& error);
// The side that answers; native builds only.
[[nodiscard]] std::unique_ptr<IWebRtcLink> CreateWebRtcAnswerer(
    std::unique_ptr<ISignalingChannel> signaling, const WebRtcConfig& config, std::string& error);

// A client's transport: datagrams to `peer` travel over the unreliable channel of a link to the server.
// Send returns WouldBlock until the link is open and Error once it has closed.
[[nodiscard]] std::unique_ptr<IDatagramTransport> CreateWebRtcClientTransport(
    std::unique_ptr<ISignalingChannel> signaling, const WebRtcConfig& config, Endpoint peer, std::string& error);

struct WebRtcServerOptions {
    WebRtcConfig link;
    std::size_t maxClients = 64U;
};

// Endpoints in 0/8 are connection identities (see WebSocketTransport.hpp). WebRTC peers take the upper
// half of the identity space so they never collide with WebSocket peers on the same host.
inline constexpr std::uint64_t kWebRtcIdentityBase = 1ULL << 39U;

// Accepts the peers whose signaling channels arrive at `signaling` and gives each an identity. Every
// other endpoint goes to `inner` (a UDP or WebSocket transport), so one Host serves all kinds of peers;
// pass nullptr to serve WebRTC peers only. Native builds only.
[[nodiscard]] std::unique_ptr<IDatagramTransport> CreateWebRtcServerTransport(const WebRtcServerOptions& options,
    std::shared_ptr<SignalingListener> signaling, std::unique_ptr<IDatagramTransport> inner, std::string& error);

} // namespace kb::net
