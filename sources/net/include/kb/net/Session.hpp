#pragma once

#include "engine/network/NetworkBudget.hpp"
#include "engine/network/NetworkSecurity.hpp"
#include "engine/security/Crypto.hpp"
#include "kb/net/Transport.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <span>
#include <vector>

// One established, authenticated connection between two peers, as a state machine that never
// touches a socket: it turns datagram bytes into messages and messages into datagram bytes. The
// caller supplies the clock, so a test can run it deterministically and fuzz it with any bytes.
//
// What it does:
//   * every datagram is sealed with XChaCha20-Poly1305 (confidentiality and authenticity) under a
//     key used in one direction only; the packet number is the nonce, so a nonce is never reused;
//   * a sliding window rejects replayed and duplicated datagrams;
//   * reliable ordered channels: acknowledgements, retransmission with a smoothed round trip time,
//     fragmentation of large messages and reassembly in order;
//   * unreliable sequenced channels: stale messages are dropped, nothing is retransmitted;
//   * every size, count and index read from the wire is checked; a bad datagram is rejected and
//     counted, and never changes the connection state.
// What it does not do (yet): congestion control, and a handshake (see Host.hpp).
namespace kb::net {

inline constexpr std::size_t kSessionKeyBytes = 32U;
using SessionKey = std::array<std::uint8_t, kSessionKeyBytes>;

enum class ChannelMode : std::uint8_t {
    ReliableOrdered,    // every message arrives, once, in the order it was sent
    UnreliableSequenced // a message may be lost; a message older than one already delivered is dropped
};

struct SessionConfig {
    std::vector<ChannelMode> channels{ ChannelMode::ReliableOrdered, ChannelMode::UnreliableSequenced };
    std::size_t maxMessageBytes = 16U * 1024U; // largest reliable message
    std::size_t maxUnreliableBytes = 1000U;    // largest unreliable message; must fit one datagram
    std::size_t maxInboxMessages = 1024U;      // delivered messages the application has not read yet
    kb::network::NetworkBudget budget{};       // packetBytes caps a datagram, maxQueuedBytes the send queue
    kb::network::NetworkSecurityLimits limits{}; // maxMessagesPerTick caps datagrams handled per peer per update
    std::uint32_t initialRtoMilliseconds = 250U;
    std::uint32_t minRtoMilliseconds = 60U;
    std::uint32_t maxRtoMilliseconds = 2000U;
    std::uint32_t keepAliveMilliseconds = 1000U;
    std::uint32_t timeoutMilliseconds = 10'000U;

    [[nodiscard]] bool IsValid() const noexcept;
};

enum class SendResult : std::uint8_t {
    Queued,
    InvalidChannel,
    TooLarge, // over maxMessageBytes or maxUnreliableBytes
    Busy,     // the send queue or the reliable window is full; try again later
    Closed,
};

enum class PacketResult : std::uint8_t {
    Accepted,
    Malformed, // too short, wrong magic or version, or a field outside its limit
    AuthFailed,
    Replay,    // a packet number already seen, or too old for the window
    Busy,      // valid but not taken (inbox or reassembly full); the sender will retransmit its content
    Closed,
};

struct Message {
    std::uint8_t channel = 0U;
    std::vector<std::uint8_t> data;
};

struct SessionStats {
    std::uint64_t packetsSent = 0U;
    std::uint64_t packetsAccepted = 0U;
    std::uint64_t bytesSent = 0U;
    std::uint64_t bytesAccepted = 0U;
    std::uint64_t rejectedMalformed = 0U;
    std::uint64_t rejectedAuth = 0U;
    std::uint64_t rejectedReplay = 0U;
    std::uint64_t rejectedBusy = 0U;
    std::uint64_t fragmentsSent = 0U;
    std::uint64_t fragmentsRetransmitted = 0U;
    std::uint64_t duplicateFragments = 0U;
    std::uint64_t staleUnreliable = 0U;
    std::uint64_t unreliableDropped = 0U;
    std::uint64_t messagesDelivered = 0U;
    std::uint32_t smoothedRttMilliseconds = 0U;
};

class Session final {
public:
    // `sendKey` seals what this side sends, `receiveKey` opens what it receives; the peer holds the
    // same two keys the other way round. `config` must be valid (SessionConfig::IsValid).
    Session(SessionConfig config, const SessionKey& sendKey, const SessionKey& receiveKey, std::uint64_t nowMilliseconds);

    [[nodiscard]] SendResult Send(std::uint8_t channel, std::span<const std::uint8_t> data);
    // Queues a close notice; the session closes once it has been sent.
    void Disconnect() noexcept { disconnectRequested_ = true; }
    // Makes the next BuildPacket produce a packet even when there is nothing to send.
    void Poke() noexcept { ackPending_ = true; }

    [[nodiscard]] PacketResult ProcessPacket(std::uint64_t nowMilliseconds, std::span<const std::uint8_t> datagram);
    // The next datagram to send, or nothing when there is nothing to say yet. Call until it returns
    // nothing, within a limit that suits the caller.
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> BuildPacket(std::uint64_t nowMilliseconds);

    [[nodiscard]] bool PopMessage(Message& message);
    [[nodiscard]] bool IsClosed() const noexcept { return closed_; }
    [[nodiscard]] bool PeerDisconnected() const noexcept { return peerDisconnected_; }
    [[nodiscard]] bool TimedOut(std::uint64_t nowMilliseconds) const noexcept;
    [[nodiscard]] std::size_t QueuedSendBytes() const noexcept { return queuedSendBytes_; }
    [[nodiscard]] const SessionStats& Stats() const noexcept { return stats_; }
    [[nodiscard]] const SessionConfig& Config() const noexcept { return config_; }

private:
    struct Fragment {
        std::vector<std::uint8_t> data;
        bool acked = false;
        bool everSent = false;
        std::uint32_t sends = 0U;
        std::uint64_t lastSent = 0U;
    };
    struct OutMessage {
        std::uint16_t id = 0U;
        std::vector<Fragment> fragments;
        std::size_t ackedFragments = 0U;
        std::size_t bytes = 0U;
    };
    struct InMessage {
        std::vector<std::vector<std::uint8_t>> parts;
        std::vector<bool> have;
        std::size_t received = 0U;
        std::size_t bytes = 0U;
    };
    struct Channel {
        ChannelMode mode = ChannelMode::ReliableOrdered;
        // Reliable ordered, send side.
        std::deque<OutMessage> out;
        std::uint16_t nextSendId = 0U;
        // Reliable ordered, receive side.
        std::uint16_t nextDeliverId = 0U;
        std::map<std::uint16_t, InMessage> reassembly;
        // Unreliable sequenced.
        std::uint16_t nextUnreliableSeq = 0U;
        bool haveUnreliable = false;
        std::uint16_t lastUnreliableSeq = 0U;
    };
    struct FragmentRef {
        std::uint8_t channel = 0U;
        std::uint16_t messageId = 0U;
        std::uint8_t fragment = 0U;
    };
    struct SentPacket {
        std::uint64_t sentAt = 0U;
        std::vector<FragmentRef> fragments;
    };
    struct ParsedRecord {
        std::uint8_t type = 0U;
        std::uint8_t channel = 0U;
        std::uint16_t id = 0U; // message id or unreliable sequence
        std::uint8_t fragment = 0U;
        std::uint8_t fragmentCount = 0U;
        std::span<const std::uint8_t> data;
    };
    struct Queued {
        std::uint8_t channel = 0U;
        std::uint16_t sequence = 0U;
        std::vector<std::uint8_t> data;
    };

    [[nodiscard]] std::size_t MaxPlaintextBytes() const noexcept;
    [[nodiscard]] std::size_t FragmentBytes() const noexcept;
    [[nodiscard]] std::uint64_t RtoFor(std::uint32_t sends) const noexcept;
    void ApplyAcks(std::uint64_t nowMilliseconds, std::uint64_t latest, std::uint32_t bits);
    void AckPacket(std::uint64_t nowMilliseconds, std::uint64_t packetNumber, bool sampleRtt);
    void AckFragment(const FragmentRef& ref);
    [[nodiscard]] PacketResult CommitRecords(const std::vector<ParsedRecord>& records);
    void DeliverReady(Channel& channel, std::uint8_t channelIndex);
    [[nodiscard]] bool IsReplay(std::uint64_t packetNumber) const noexcept;
    void MarkReceived(std::uint64_t packetNumber) noexcept;

    SessionConfig config_;
    kb::security::AeadKey sendKey_;
    kb::security::AeadKey receiveKey_;
    std::vector<Channel> channels_;
    std::deque<Queued> unreliableOut_;
    std::deque<Message> inbox_;
    std::map<std::uint64_t, SentPacket> sent_;
    SessionStats stats_;

    std::uint64_t nextPacketNumber_ = 1U;
    std::uint64_t highestReceived_ = 0U;
    std::uint64_t receivedBitmap_ = 0U; // bit i: packet (highestReceived_ - i) was received
    std::uint64_t lastReceiveMs_ = 0U;
    std::uint64_t lastSendMs_ = 0U;
    std::size_t queuedSendBytes_ = 0U;
    std::size_t bufferedReassemblyBytes_ = 0U;
    std::uint32_t rttVarianceMs_ = 0U;
    std::uint32_t disconnectNotices_ = 0U;
    bool haveRtt_ = false;
    bool ackPending_ = true; // the first packet tells the peer we exist
    bool disconnectRequested_ = false;
    bool peerDisconnected_ = false;
    bool closed_ = false;
};

} // namespace kb::net
