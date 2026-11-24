#pragma once

#include "kb/net/Session.hpp"
#include "kb/net/Diagnostics.hpp"
#include "kb/net/Transport.hpp"

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

// Connection establishment on top of IDatagramTransport, and the host and client objects that own
// Sessions.
//
// Authentication. Each client has a 32 byte secret key that the host also knows (handed out by a
// login or matchmaking service over a channel the application already trusts, never sent in the
// clear here). The handshake proves both sides know it without revealing it:
//
//   client -> host  Connect    client id, client salt, build identity, padded to 128 bytes
//   host   -> client Challenge  client salt, server salt, MAC(key, ...)    (the host keeps no state)
//   client -> host  Response   client id, salts, MAC(key, ...)
//   host   -> client Accept     random nonce, authenticated encrypted server random and identity
//
// The server salt is a cookie derived from a host secret and the client address, so a Response can
// only come from the address that received the Challenge, and the host stores nothing for a Connect
// that never completes. The Challenge is shorter than the Connect, so the host cannot be used to
// amplify traffic towards a spoofed address. Accept is sealed with a separate HKDF key. Only then
// does the client create its Session; session keys include fresh server randomness. A duplicate
// Response gets the cached Accept without resetting keys or packet numbers. Unconfirmed peers
// expire after HostConfig::confirmMilliseconds, regardless of repeated Responses.
//
// A Connect for an id the host does not know is answered like any other (with a Challenge made from a key derived from the host's
// secret), so the answer does not tell which ids exist; the Response then fails. Authenticated Responses are rate limited per
// proven address before key agreement, and an address holds a bounded number of sessions.
//
// Ephemeral X25519 keys contribute to session keys and are wiped after agreement. Key distribution is external.
namespace kb::net {

inline constexpr std::size_t kMinConnectBytes = 128U;

// Looks up the secret key of a client id. Return false for an unknown or banned client.
class IKeyProvider {
public:
    IKeyProvider() = default;
    IKeyProvider(const IKeyProvider&) = delete;
    IKeyProvider& operator=(const IKeyProvider&) = delete;
    virtual ~IKeyProvider() = default;
    [[nodiscard]] virtual bool LookupKey(std::uint64_t clientId, SessionKey& key) const = 0;
};

class StaticKeyTable final : public IKeyProvider {
public:
    void Set(std::uint64_t clientId, const SessionKey& key) { keys_[clientId] = key; }
    void Remove(std::uint64_t clientId) { keys_.erase(clientId); }
    [[nodiscard]] bool LookupKey(std::uint64_t clientId, SessionKey& key) const override {
        const auto found = keys_.find(clientId);
        if (found == keys_.end()) {
            return false;
        }
        key = found->second;
        return true;
    }

private:
    std::map<std::uint64_t, SessionKey> keys_;
};

enum class NetEventType : std::uint8_t { Connected, Disconnected, Message };

enum class DisconnectReason : std::uint8_t {
    None,
    Requested,      // this side asked for it
    PeerClosed,     // the other side said goodbye
    Timeout,        // nothing arrived for SessionConfig::timeoutMilliseconds
    ConnectTimeout, // the handshake did not finish
    Replaced,       // the same client id connected from another address
    Error,          // the key or the random source could not be obtained
    VersionMismatch,
    ContentMismatch,
    OutdatedClient,
    ServerFull,
};

struct NetEvent {
    NetEventType type = NetEventType::Message;
    std::uint64_t peer = 0U; // client id; on a client, the id of the host side is 0
    DisconnectReason reason = DisconnectReason::None;
    Message message;
};

struct HostStats {
    std::uint64_t datagramsReceived = 0U;
    std::uint64_t oversized = 0U;
    std::uint64_t malformed = 0U;
    std::uint64_t challengesSent = 0U;
    std::uint64_t handshakesCompleted = 0U;
    std::uint64_t handshakesRejected = 0U; // wrong MAC, stale cookie, unknown client, host full
    std::uint64_t unknownPeer = 0U;        // sealed data from an address with no session
    std::uint64_t rateLimited = 0U;
    std::uint64_t sendErrors = 0U;
    std::uint64_t handshakesRateLimited = 0U;     // authenticated Responses over the per-address rate: dropped before key agreement
    std::uint64_t sessionsPerAddressRefused = 0U; // proven Responses refused because the address already holds its share of sessions
    std::uint64_t connectBucketsFull = 0U;       // authenticated Responses admitted without a bucket when the table remains full
};

struct BuildIdentity {
    std::uint32_t release = 0U;
    std::uint64_t contentHash = 0U;
};

[[nodiscard]] BuildIdentity MakeBuildIdentity(std::uint32_t release, std::string_view contentVersion) noexcept;

enum class RejectReason : std::uint8_t { ProtocolVersion = 1U, OutdatedClient = 2U, ContentMismatch = 3U, ServerFull = 4U };

struct HandshakeRejectInfo {
    RejectReason reason = RejectReason::ProtocolVersion;
    BuildIdentity server;
};

struct HostConfig {
    SessionConfig session{};
    BuildIdentity identity{};
    std::size_t maxClients = 64U;
    std::size_t maxDatagramsPerUpdate = 1024U; // bound on the work one Update does for any sender
    std::size_t maxPacketsPerPeerPerUpdate = 16U;
    std::uint64_t cookieLifetimeMilliseconds = 30'000U;
    std::uint32_t minClientRelease = 0U;
    std::uint32_t confirmMilliseconds = 5000U;
    // Per IPv4 address (the port does not count): Connects a second (also the burst), and open sessions. Raise the session limit for
    // players behind one shared address or for many test clients on one machine.
    std::uint32_t maxConnectsPerSecondPerAddress = 8U;
    std::uint32_t maxSessionsPerAddress = 16U;
};

class Host final {
public:
    // `transport` and `keys` must outlive the host. `Valid()` is false when the configuration is
    // invalid or the system could not supply random bytes for the cookie secret.
    Host(IDatagramTransport& transport, HostConfig config, const IKeyProvider& keys);

    [[nodiscard]] bool Valid() const noexcept { return valid_; }
    void Update(std::uint64_t nowMilliseconds);
    [[nodiscard]] SendResult Send(std::uint64_t clientId, std::uint8_t channel, std::span<const std::uint8_t> data);
    void Disconnect(std::uint64_t clientId);
    [[nodiscard]] bool PollEvent(NetEvent& event);
    [[nodiscard]] std::size_t ClientCount() const noexcept { return peers_.size(); }
    // Ids of the clients that are connected now.
    [[nodiscard]] std::vector<std::uint64_t> ClientIds() const;
    [[nodiscard]] DiagLog& Diagnostics() noexcept { return diagnostics_; }
    [[nodiscard]] const HostStats& Stats() const noexcept { return stats_; }
    [[nodiscard]] const SessionStats* ClientStats(std::uint64_t clientId) const noexcept;

private:
    struct ConnectBucket {
        double tokens = 0.0;
        std::uint64_t updatedAt = 0U;
    };
    struct Peer {
        Endpoint endpoint;
        std::uint64_t clientId = 0U;
        std::uint64_t clientSalt = 0U;
        std::uint64_t serverSalt = 0U;
        std::array<std::uint8_t, 32U> clientEphemeral{};
        std::vector<std::uint8_t> accept;
        std::uint64_t createdAt = 0U;
        bool confirmed = false;
        std::unique_ptr<Session> session;
        std::size_t packetsThisUpdate = 0U;
    };

    void SendReject(const Endpoint& to, RejectReason reason, std::uint64_t clientSalt);
    void HandleConnect(std::uint64_t nowMilliseconds, const Endpoint& from, std::span<const std::uint8_t> datagram);
    void HandleResponse(std::uint64_t nowMilliseconds, const Endpoint& from, std::span<const std::uint8_t> datagram);
    void HandleData(std::uint64_t nowMilliseconds, const Endpoint& from, std::span<const std::uint8_t> datagram);
    void DrainMessages(Peer& peer);
    [[nodiscard]] bool AllowConnect(std::uint64_t nowMilliseconds, std::uint32_t address);
    [[nodiscard]] SessionKey DecoyKey(std::uint64_t clientId) const;
    void RemovePeer(std::map<Endpoint, Peer>::iterator found, DisconnectReason reason);
    [[nodiscard]] std::uint64_t Cookie(const Endpoint& from, std::uint64_t clientId, std::uint64_t clientSalt, std::uint64_t epoch) const;

    IDatagramTransport* transport_;
    const IKeyProvider* keys_;
    HostConfig config_;
    SessionKey cookieSecret_{};
    bool valid_ = false;
    std::map<Endpoint, Peer> peers_;
    std::map<std::uint64_t, Endpoint> byClient_;
    std::unordered_map<std::uint32_t, ConnectBucket> connectBuckets_;
    std::uint64_t lastBucketSweep_ = 0U;
    std::deque<NetEvent> events_;
    HostStats stats_;
    DiagLog diagnostics_;
    std::vector<std::uint8_t> scratch_;
};

enum class ClientState : std::uint8_t { Connecting, Connected, Disconnected };

struct ClientConfig {
    SessionConfig session{};
    BuildIdentity identity{};
    std::uint32_t retryMilliseconds = 250U;
    std::uint32_t connectTimeoutMilliseconds = 5000U;
    std::uint64_t clientSalt = 0U; // 0 draws a random one; tests may fix it
    // Frame application messages for AuthGate. Authenticate after the transport connects;
    // start replication only after receiving kAuthMessageAccepted.
    bool authorizationRequired = false;
};

class Client final {
public:
    Client(IDatagramTransport& transport, ClientConfig config, std::uint64_t clientId, const SessionKey& key, Endpoint host,
        std::uint64_t nowMilliseconds);

    void Update(std::uint64_t nowMilliseconds);
    [[nodiscard]] SendResult Send(std::uint8_t channel, std::span<const std::uint8_t> data);
    [[nodiscard]] SendResult Authenticate(std::string_view token);
    void Disconnect();
    [[nodiscard]] bool PollEvent(NetEvent& event);
    [[nodiscard]] const HandshakeRejectInfo* RejectInfo() const noexcept { return rejected_ ? &rejectInfo_ : nullptr; }
    [[nodiscard]] DiagLog& Diagnostics() noexcept { return diagnostics_; }
    [[nodiscard]] ClientState State() const noexcept { return state_; }
    [[nodiscard]] const SessionStats* Stats() const noexcept { return session_ != nullptr ? &session_->Stats() : nullptr; }

private:
    enum class Stage : std::uint8_t { Connect, Response, Established };

    void HandleDatagram(std::uint64_t nowMilliseconds, std::span<const std::uint8_t> datagram);
    void Fail(DisconnectReason reason);
    void SendConnect();
    void SendResponse();

    IDatagramTransport* transport_;
    ClientConfig config_;
    std::uint64_t clientId_;
    SessionKey key_;
    kb::security::X25519KeyPair ephemeral_;
    Endpoint host_;
    std::uint64_t clientSalt_ = 0U;
    std::uint64_t serverSalt_ = 0U;
    std::uint64_t startedAt_;
    std::uint64_t lastHandshakeSend_;
    bool sentFirst_ = false;
    HandshakeRejectInfo rejectInfo_;
    bool rejected_ = false;
    DiagLog diagnostics_;
    Stage stage_ = Stage::Connect;
    ClientState state_ = ClientState::Connecting;
    std::unique_ptr<Session> session_;
    std::deque<NetEvent> events_;
    std::vector<std::uint8_t> scratch_;
};

} // namespace kb::net
