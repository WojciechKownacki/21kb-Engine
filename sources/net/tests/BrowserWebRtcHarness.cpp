// The browser side of the WebRTC test: a client transport whose datagrams the native listener echoes.

#include "kb/net/WebRtcTransport.hpp"

#include <emscripten/emscripten.h>
#include <array>

namespace {
std::unique_ptr<kb::net::IDatagramTransport> transport;
constexpr auto peer = kb::net::Endpoint::Loopback(1U);
}

extern "C" {
EMSCRIPTEN_KEEPALIVE int OpenLink() {
    std::string error;
    auto signaling = kb::net::CreateWebSocketSignalingClient(emscripten_run_script_string("window.testSocketUrl"), error);
    if (!signaling) return 0;
    kb::net::WebRtcConfig config;
    config.timeoutMilliseconds = 3000U;
    transport = kb::net::CreateWebRtcClientTransport(std::move(signaling), config, peer, error);
    return transport ? 1 : 0;
}
EMSCRIPTEN_KEEPALIVE void CloseLink() { transport.reset(); }
EMSCRIPTEN_KEEPALIVE int SendPacket(int length, int seed) {
    if (!transport || length < 0 || length > 1201) return -1;
    std::array<std::uint8_t, 1201> bytes{};
    for (int i = 0; i < length; ++i) bytes[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i + seed);
    return static_cast<int>(transport->Send(peer, std::span<const std::uint8_t>(bytes.data(), static_cast<std::size_t>(length))));
}
EMSCRIPTEN_KEEPALIVE int CheckPacket(int length, int seed) {
    if (!transport || length < 0 || length > 1200) return -1;
    std::array<std::uint8_t, 1200> bytes{};
    kb::net::Endpoint from;
    std::size_t size = 0U;
    const auto status = transport->Receive(from, bytes, size);
    if (status != kb::net::TransportStatus::Ok) return static_cast<int>(status);
    if (from != peer || size != static_cast<std::size_t>(length)) return -2;
    for (int i = 0; i < length; ++i)
        if (bytes[static_cast<std::size_t>(i)] != static_cast<std::uint8_t>(i + seed)) return -3;
    return 0;
}
}
int main() { emscripten_run_script("window.testSocketReady=true"); }
