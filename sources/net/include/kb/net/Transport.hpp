#pragma once

#include "engine/network/NetworkSimulation.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

// Datagram transport behind one interface. The session layer (Session.hpp, Host.hpp) only talks to
// IDatagramTransport, so the same code runs over real UDP sockets (Windows and POSIX) and over the
// in-process LoopbackNetwork that tests use to inject loss, duplication, reordering and latency.
namespace kb::net {

// The largest datagram the protocol builds or accepts. It stays under the usual 1280 byte IPv6
// minimum MTU after IP and UDP headers, so packets are not fragmented by the network.
inline constexpr std::size_t kMaxDatagramBytes = 1200U;

// IPv4 endpoint, address in host byte order.
struct Endpoint {
    std::uint32_t address = 0U;
    std::uint16_t port = 0U;

    [[nodiscard]] auto operator<=>(const Endpoint&) const noexcept = default;
    [[nodiscard]] static constexpr Endpoint Make(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d, std::uint16_t port) noexcept {
        return Endpoint{ (static_cast<std::uint32_t>(a) << 24U) | (static_cast<std::uint32_t>(b) << 16U) |
                (static_cast<std::uint32_t>(c) << 8U) | static_cast<std::uint32_t>(d), port };
    }
    [[nodiscard]] static constexpr Endpoint Loopback(std::uint16_t port) noexcept { return Make(127U, 0U, 0U, 1U, port); }
    [[nodiscard]] std::string ToString() const;
};

enum class TransportStatus : std::uint8_t {
    Ok,         // the call did its work
    WouldBlock, // nothing to receive right now
    TooLarge,   // the datagram is over kMaxDatagramBytes
    Error,      // the socket or network reported a failure
};

class IDatagramTransport {
public:
    IDatagramTransport() = default;
    IDatagramTransport(const IDatagramTransport&) = delete;
    IDatagramTransport& operator=(const IDatagramTransport&) = delete;
    virtual ~IDatagramTransport() = default;

    // Never blocks. A datagram handed to Send may still be lost: the transport is unreliable.
    [[nodiscard]] virtual TransportStatus Send(const Endpoint& to, std::span<const std::uint8_t> datagram) = 0;
    // Never blocks. On Ok, `size` is the datagram length. A datagram longer than `buffer` is
    // reported with size == buffer.size(), so a caller that passes kMaxDatagramBytes + 1 bytes
    // can tell an oversized datagram from a legal one.
    [[nodiscard]] virtual TransportStatus Receive(Endpoint& from, std::span<std::uint8_t> buffer, std::size_t& size) = 0;
    [[nodiscard]] virtual Endpoint LocalEndpoint() const = 0;
};

// Conditions of the simulated link. Loss, reordering and latency come from the engine's
// deterministic NetworkSimulationConfig, so a seed reproduces a run exactly; duplication is added
// here.
struct LinkConditions {
    kb::network::NetworkSimulationConfig simulation{};
    std::uint16_t duplicatePermille = 0U;
};

struct LoopbackStats {
    std::uint64_t sent = 0U;
    std::uint64_t dropped = 0U;
    std::uint64_t duplicated = 0U;
    std::uint64_t reordered = 0U;
    std::uint64_t delivered = 0U;
    std::uint64_t inboxOverflow = 0U;
};

// An in-process network of transports. Time only moves when Advance is called, which delivers every
// packet that is due, so a test is deterministic and needs no sleeping.
class LoopbackNetwork final {
public:
    explicit LoopbackNetwork(LinkConditions conditions = {});
    LoopbackNetwork(const LoopbackNetwork&) = delete;
    LoopbackNetwork& operator=(const LoopbackNetwork&) = delete;
    ~LoopbackNetwork();

    // Returns nullptr when the endpoint is already taken. The transport must not outlive the network.
    [[nodiscard]] std::unique_ptr<IDatagramTransport> CreateTransport(Endpoint local);
    void SetConditions(LinkConditions conditions) noexcept { conditions_ = conditions; }
    void Advance(std::uint64_t nowMilliseconds);
    // Puts a datagram straight into the destination inbox, bypassing the link conditions. For tests
    // that send hostile bytes.
    void Inject(const Endpoint& from, const Endpoint& to, std::span<const std::uint8_t> datagram);
    [[nodiscard]] const LoopbackStats& Stats() const noexcept { return stats_; }
    [[nodiscard]] std::size_t InFlight() const noexcept { return inFlight_.size(); }

private:
    class Port;
    struct Packet {
        std::uint64_t deliverAt = 0U;
        std::uint64_t order = 0U;
        Endpoint from;
        Endpoint to;
        std::vector<std::uint8_t> data;
    };

    TransportStatus Submit(const Endpoint& from, const Endpoint& to, std::span<const std::uint8_t> datagram);
    void Deliver(Packet&& packet);
    void Detach(const Endpoint& endpoint) noexcept;

    LinkConditions conditions_;
    std::uint64_t now_ = 0U;
    std::uint64_t counter_ = 0U;
    std::map<Endpoint, Port*> ports_;
    std::vector<Packet> inFlight_;
    LoopbackStats stats_;
};

// Non-blocking UDP over Winsock or BSD sockets. Returns nullptr and fills `error` when the socket
// cannot be created or bound, or on a platform without sockets (the browser build). Port 0 asks the
// system for a free port; LocalEndpoint() then reports the one that was chosen.
[[nodiscard]] std::unique_ptr<IDatagramTransport> CreateUdpTransport(Endpoint bindTo, std::string& error);

} // namespace kb::net
