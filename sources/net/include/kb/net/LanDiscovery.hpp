#pragma once

#include "kb/net/Transport.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Finding games on the local network without a server list: a host broadcasts a small plain packet
// every two seconds, a player's browser sends a query and collects the answers. The packets carry no
// secrets, so anyone on the link can read a game's name and player count; the join password and the
// session keys never leave the session handshake. Everything here runs over IDatagramTransport, so
// it is deterministic on the in-process LoopbackNetwork in tests.
namespace kb::net {

inline constexpr std::uint16_t kLanDiscoveryPort = 41821U;
inline constexpr std::uint8_t kLanDiscoveryProtocol = 1U;
// Query and announce are both sent in this many bytes, so an announce never amplifies a query.
inline constexpr std::size_t kLanDiscoveryDatagramBytes = 96U;
inline constexpr std::uint32_t kLanBeaconIntervalMilliseconds = 2000U;
inline constexpr std::uint32_t kLanBrowserIntervalMilliseconds = 2000U;
inline constexpr std::uint32_t kLanGameExpiryMilliseconds = 6000U;
inline constexpr std::size_t kLanGameNameBytes = 32U;
inline constexpr std::size_t kLanMaxGames = 64U;

// One game seen on the local network. `address` is where the announce came from (the discovery port);
// `port` is the game's join port.
struct LanGame {
    Endpoint address{};
    std::uint16_t port = 0U;
    std::uint16_t players = 0U;
    std::uint16_t maxPlayers = 0U;
    bool passwordRequired = false;
    std::string name;
    std::uint64_t gameKey = 0U;
    std::uint64_t hostSalt = 0U;
    std::uint64_t lastSeenMilliseconds = 0U;
};

// Where discovery packets go: the directed broadcast address of every usable IPv4 interface, plus
// the limited broadcast 255.255.255.255 (which leaves only one interface on a multi-homed machine).
[[nodiscard]] std::vector<Endpoint> ListBroadcastEndpoints(std::uint16_t port);

struct LanBeaconOptions {
    std::uint64_t gameKey = 0U;        // content hash; browsers look only for their own game
    std::uint16_t hostPort = 0U;       // the port players join
    std::uint16_t maxPlayers = 0U;
    bool passwordRequired = false;
    std::uint64_t hostSalt = 0U;       // lets a browser tell two games from one machine apart
    std::string name;                  // at most kLanGameNameBytes bytes of UTF-8, cut on a codepoint
    // Announce destinations. Empty: ListBroadcastEndpoints(kLanDiscoveryPort).
    std::vector<Endpoint> announceTargets;
};

// The host side: answers a query with a unicast announce and broadcasts one every two seconds.
// Update never blocks; call it from the game loop. At most ten answers go out per second.
class LanBeacon final {
public:
    LanBeacon(IDatagramTransport& transport, LanBeaconOptions options);
    LanBeacon(const LanBeacon&) = delete;
    LanBeacon& operator=(const LanBeacon&) = delete;
    void SetPlayers(std::uint16_t players) noexcept { players_ = players; }
    void Update(std::uint64_t nowMilliseconds);

private:
    void Announce(const Endpoint& to, std::uint32_t nonce, std::uint64_t nowMilliseconds);

    IDatagramTransport& transport_;
    LanBeaconOptions options_;
    std::vector<std::uint8_t> announce_;
    std::vector<Endpoint> targets_;
    std::vector<std::uint64_t> answers_;
    std::uint64_t nextAnnounceAt_ = 0U;
    std::uint32_t nonce_ = 0U;
    std::uint16_t players_ = 0U;
    bool started_ = false;
};

// The player side: sends a query every two seconds, collects the announces and drops a game that has
// not announced for six seconds. Update never blocks; call it from the game loop.
class LanBrowser final {
public:
    explicit LanBrowser(IDatagramTransport& transport, std::uint64_t gameKey = 0U);
    LanBrowser(IDatagramTransport& transport, std::uint64_t gameKey, std::vector<Endpoint> queryTargets);
    LanBrowser(const LanBrowser&) = delete;
    LanBrowser& operator=(const LanBrowser&) = delete;
    void Update(std::uint64_t nowMilliseconds);
    [[nodiscard]] const std::vector<LanGame>& Games() const noexcept { return games_; }

private:
    void Query(std::uint64_t nowMilliseconds);

    IDatagramTransport& transport_;
    std::uint64_t gameKey_;
    std::vector<Endpoint> targets_;
    std::vector<LanGame> games_;
    std::uint64_t nextQueryAt_ = 0U;
    std::uint32_t nonce_ = 0U;
    bool started_ = false;
};

} // namespace kb::net
