#include "kb/net/Transport.hpp"

#if defined(__EMSCRIPTEN__)

namespace kb::net {

std::unique_ptr<IDatagramTransport> CreateUdpTransport(Endpoint, std::string& error) {
    error = "UDP sockets are not available in the browser build";
    return nullptr;
}

} // namespace kb::net

#else

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <algorithm>

namespace kb::net {

namespace {

#if defined(_WIN32)
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;

// Winsock must be started once per process and may be shared by every socket.
class WinsockSession final {
public:
    WinsockSession() noexcept {
        WSADATA data{};
        ok_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    ~WinsockSession() {
        if (ok_) {
            WSACleanup();
        }
    }
    WinsockSession(const WinsockSession&) = delete;
    WinsockSession& operator=(const WinsockSession&) = delete;
    [[nodiscard]] bool Ok() const noexcept { return ok_; }

private:
    bool ok_ = false;
};

[[nodiscard]] bool EnsureNetworkStack() noexcept {
    static const WinsockSession session;
    return session.Ok();
}

void CloseSocket(SocketHandle handle) noexcept { closesocket(handle); }
[[nodiscard]] bool SetNonBlocking(SocketHandle handle) noexcept {
    u_long enable = 1U;
    return ioctlsocket(handle, FIONBIO, &enable) == 0;
}
[[nodiscard]] int LastError() noexcept { return WSAGetLastError(); }
[[nodiscard]] bool IsWouldBlock(int code) noexcept { return code == WSAEWOULDBLOCK; }
// An ICMP "port unreachable" answer to an earlier send is reported by the next receive; it says
// nothing about the datagram waiting behind it.
[[nodiscard]] bool IsConnectionReset(int code) noexcept { return code == WSAECONNRESET; }
using SocketLength = int;
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;

[[nodiscard]] bool EnsureNetworkStack() noexcept { return true; }
void CloseSocket(SocketHandle handle) noexcept { ::close(handle); }
[[nodiscard]] bool SetNonBlocking(SocketHandle handle) noexcept {
    const int flags = fcntl(handle, F_GETFL, 0);
    return flags >= 0 && fcntl(handle, F_SETFL, flags | O_NONBLOCK) == 0;
}
[[nodiscard]] int LastError() noexcept { return errno; }
[[nodiscard]] bool IsWouldBlock(int code) noexcept { return code == EAGAIN || code == EWOULDBLOCK; }
[[nodiscard]] bool IsConnectionReset(int code) noexcept { return code == ECONNREFUSED || code == ECONNRESET; }
using SocketLength = socklen_t;
#endif

[[nodiscard]] sockaddr_in ToSockaddr(const Endpoint& endpoint) noexcept {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(endpoint.port);
    address.sin_addr.s_addr = htonl(endpoint.address);
    return address;
}

[[nodiscard]] Endpoint FromSockaddr(const sockaddr_in& address) noexcept {
    return Endpoint{ ntohl(address.sin_addr.s_addr), ntohs(address.sin_port) };
}

class UdpTransport final : public IDatagramTransport {
public:
    UdpTransport(SocketHandle handle, Endpoint local) noexcept : handle_(handle), local_(local) {}
    ~UdpTransport() override { CloseSocket(handle_); }

    [[nodiscard]] TransportStatus Send(const Endpoint& to, std::span<const std::uint8_t> datagram) override {
        if (datagram.size() > kMaxDatagramBytes) {
            return TransportStatus::TooLarge;
        }
        const sockaddr_in address = ToSockaddr(to);
        const auto sent = sendto(handle_, reinterpret_cast<const char*>(datagram.data()), static_cast<int>(datagram.size()), 0,
            reinterpret_cast<const sockaddr*>(&address), static_cast<SocketLength>(sizeof(address)));
        if (sent < 0) {
            // A full send buffer drops the datagram, which an unreliable transport is allowed to do.
            return IsWouldBlock(LastError()) ? TransportStatus::Ok : TransportStatus::Error;
        }
        return TransportStatus::Ok;
    }

    [[nodiscard]] TransportStatus Receive(Endpoint& from, std::span<std::uint8_t> buffer, std::size_t& size) override {
        size = 0U;
        // Skip the "connection reset" notices Windows and Linux raise for earlier sends; bounded so a
        // flood of them cannot hold the caller.
        for (int attempt = 0; attempt < 16; ++attempt) {
            sockaddr_in address{};
            SocketLength length = static_cast<SocketLength>(sizeof(address));
            const auto received = recvfrom(handle_, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0,
                reinterpret_cast<sockaddr*>(&address), &length);
            if (received >= 0) {
                from = FromSockaddr(address);
                size = std::min(static_cast<std::size_t>(received), buffer.size());
                return TransportStatus::Ok;
            }
            const int code = LastError();
            if (IsWouldBlock(code)) {
                return TransportStatus::WouldBlock;
            }
            if (!IsConnectionReset(code)) {
                return TransportStatus::Error;
            }
        }
        return TransportStatus::WouldBlock;
    }

    [[nodiscard]] Endpoint LocalEndpoint() const override { return local_; }

private:
    SocketHandle handle_;
    Endpoint local_;
};

} // namespace

std::unique_ptr<IDatagramTransport> CreateUdpTransport(Endpoint bindTo, std::string& error) {
    if (!EnsureNetworkStack()) {
        error = "the network stack could not be started";
        return nullptr;
    }
    const SocketHandle handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (handle == kInvalidSocket) {
        error = "socket() failed with code " + std::to_string(LastError());
        return nullptr;
    }
    if (!SetNonBlocking(handle)) {
        error = "the socket could not be made non-blocking";
        CloseSocket(handle);
        return nullptr;
    }
    const sockaddr_in address = ToSockaddr(bindTo);
    if (bind(handle, reinterpret_cast<const sockaddr*>(&address), static_cast<SocketLength>(sizeof(address))) != 0) {
        error = "bind() to " + bindTo.ToString() + " failed with code " + std::to_string(LastError());
        CloseSocket(handle);
        return nullptr;
    }
    sockaddr_in bound{};
    SocketLength length = static_cast<SocketLength>(sizeof(bound));
    if (getsockname(handle, reinterpret_cast<sockaddr*>(&bound), &length) != 0) {
        error = "getsockname() failed with code " + std::to_string(LastError());
        CloseSocket(handle);
        return nullptr;
    }
    return std::make_unique<UdpTransport>(handle, FromSockaddr(bound));
}

} // namespace kb::net

#endif
