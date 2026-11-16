// A native listener for the browser WebRTC test: signaling on the WebSocket, datagrams echoed back.

#include "kb/net/WebRtcTransport.hpp"
#include "kb/net/WebSocketTransport.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    auto signaling = std::make_shared<kb::net::SignalingListener>();
    kb::net::WebSocketServerOptions web;
    web.udpBind = kb::net::Endpoint::Loopback(0U);
    web.allowedOrigin = argv[2];
    web.maxClients = 4U;
    web.signaling = signaling;
    std::uint16_t port = 0U;
    std::string error;
    auto listener = kb::net::CreateWebSocketServerTransport(web, port, error);
    if (!listener) { std::cerr << error << '\n'; return 1; }
    kb::net::WebRtcServerOptions options;
    options.maxClients = 4U;
    options.link.timeoutMilliseconds = 3000U;
    auto transport = kb::net::CreateWebRtcServerTransport(options, signaling, std::move(listener), error);
    if (!transport) { std::cerr << error << '\n'; return 1; }
    std::cout << port << std::endl;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    std::array<std::uint8_t, kb::net::kMaxDatagramBytes> bytes{};
    while (!std::filesystem::exists(argv[1])) {
        if (std::chrono::steady_clock::now() >= deadline) return 3;
        kb::net::Endpoint from;
        std::size_t size = 0U;
        const auto status = transport->Receive(from, bytes, size);
        if (status == kb::net::TransportStatus::Error) return 4;
        if (status == kb::net::TransportStatus::Ok) {
            static_cast<void>(transport->Send(from, std::span<const std::uint8_t>(bytes.data(), size)));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    return 0;
}
