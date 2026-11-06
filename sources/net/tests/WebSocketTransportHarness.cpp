#include "kb/net/WebSocketTransport.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) return 2;
    kb::net::WebSocketServerOptions options;
    options.udpBind = kb::net::Endpoint::Loopback(0U);
    options.allowedOrigin = argc == 3 ? argv[2] : "http://localhost";
    options.maxClients = 2U;
    std::uint16_t port = 0U;
    std::string error;
    auto transport = kb::net::CreateWebSocketServerTransport(options, port, error);
    if (!transport) { std::cerr << error << '\n'; return 1; }
    std::cout << port << ' ' << transport->LocalEndpoint().port << std::endl;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
    std::array<std::uint8_t, kb::net::kMaxDatagramBytes> bytes{};
    while (!std::filesystem::exists(argv[1])) {
        if (std::chrono::steady_clock::now() >= deadline) return 3;
        kb::net::Endpoint from;
        std::size_t size = 0U;
        const auto status = transport->Receive(from, bytes, size);
        if (status == kb::net::TransportStatus::Error) return 4;
        if (status == kb::net::TransportStatus::Ok &&
            transport->Send(from, std::span<const std::uint8_t>(bytes.data(), size)) != kb::net::TransportStatus::Ok) return 5;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return 0;
}
