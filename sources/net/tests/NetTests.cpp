// Tests for kb_net: wire reading, sessions, the handshake, hostile input and a bad network.
// Every test is deterministic: time is passed in, the simulated network is seeded, and the fuzz
// generator is fixed.

#include "kb/net/ByteIO.hpp"
#include "kb/net/Host.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace kb::net;

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

SessionKey MakeKey(std::uint8_t seed) {
    SessionKey key{};
    for (std::size_t index = 0U; index < key.size(); ++index) {
        key[index] = static_cast<std::uint8_t>(seed + index * 7U);
    }
    return key;
}

std::vector<std::uint8_t> Pattern(std::uint32_t id, std::size_t size) {
    std::vector<std::uint8_t> data(std::max<std::size_t>(size, 4U));
    for (std::size_t index = 0U; index < data.size(); ++index) {
        data[index] = static_cast<std::uint8_t>((id * 31U + index * 13U) & 0xFFU);
    }
    data[0] = static_cast<std::uint8_t>(id & 0xFFU);
    data[1] = static_cast<std::uint8_t>((id >> 8U) & 0xFFU);
    data[2] = static_cast<std::uint8_t>((id >> 16U) & 0xFFU);
    data[3] = static_cast<std::uint8_t>((id >> 24U) & 0xFFU);
    return data;
}

std::uint32_t IdOf(const std::vector<std::uint8_t>& data) {
    return static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8U) |
        (static_cast<std::uint32_t>(data[2]) << 16U) | (static_cast<std::uint32_t>(data[3]) << 24U);
}

// xorshift64: the same sequence on every platform.
struct Random {
    std::uint64_t state;
    std::uint64_t Next() noexcept {
        state ^= state << 13U;
        state ^= state >> 7U;
        state ^= state << 17U;
        return state;
    }
    std::size_t Below(std::size_t limit) noexcept { return static_cast<std::size_t>(Next() % limit); }
};

// Forwards to another transport and remembers every datagram sent through it.
class TappingTransport final : public IDatagramTransport {
public:
    explicit TappingTransport(IDatagramTransport& inner) : inner_(&inner) {}
    TransportStatus Send(const Endpoint& to, std::span<const std::uint8_t> datagram) override {
        if (sent.size() < 4096U) {
            sent.emplace_back(datagram.begin(), datagram.end());
        }
        return inner_->Send(to, datagram);
    }
    TransportStatus Receive(Endpoint& from, std::span<std::uint8_t> buffer, std::size_t& size) override {
        return inner_->Receive(from, buffer, size);
    }
    Endpoint LocalEndpoint() const override { return inner_->LocalEndpoint(); }

    std::vector<std::vector<std::uint8_t>> sent;

private:
    IDatagramTransport* inner_;
};

// Two sessions with matching keys, joined by direct calls.
struct SessionPair {
    explicit SessionPair(const SessionConfig& config = {})
        : a(config, MakeKey(1U), MakeKey(2U), 0U), b(config, MakeKey(2U), MakeKey(1U), 0U) {}

    Session a;
    Session b;
};

std::size_t Exchange(Session& from, Session& to, std::uint64_t now, std::size_t limit = 64U) {
    std::size_t packets = 0U;
    while (packets < limit) {
        const auto packet = from.BuildPacket(now);
        if (!packet.has_value()) {
            break;
        }
        static_cast<void>(to.ProcessPacket(now, *packet));
        ++packets;
    }
    return packets;
}

struct Rig {
    explicit Rig(LinkConditions conditions = {}, HostConfig hostConfig = {}, ClientConfig clientConfig = {}, std::uint64_t clientId = 42U,
        SessionKey clientKey = MakeKey(9U))
        : network(conditions) {
        hostPort = network.CreateTransport(Endpoint::Loopback(7000));
        clientPort = network.CreateTransport(Endpoint::Loopback(7001));
        hostTap = std::make_unique<TappingTransport>(*hostPort);
        clientTap = std::make_unique<TappingTransport>(*clientPort);
        keys.Set(42U, MakeKey(9U));
        host = std::make_unique<Host>(*hostTap, hostConfig, keys);
        client = std::make_unique<Client>(*clientTap, clientConfig, clientId, clientKey, hostPort->LocalEndpoint(), 0U);
    }

    void Step(std::uint64_t now) {
        network.Advance(now);
        host->Update(now);
        client->Update(now);
        NetEvent event;
        while (host->PollEvent(event)) {
            hostEvents.push_back(std::move(event));
        }
        while (client->PollEvent(event)) {
            clientEvents.push_back(std::move(event));
        }
    }

    // Runs until `done` or the simulated time limit; returns the time reached.
    std::uint64_t Run(std::uint64_t from, std::uint64_t until, const std::function<bool()>& done) {
        std::uint64_t now = from;
        for (; now < until; now += 10U) {
            Step(now);
            if (done()) {
                break;
            }
        }
        return now;
    }

    [[nodiscard]] static bool HasEvent(const std::vector<NetEvent>& events, NetEventType type) {
        return std::ranges::any_of(events, [type](const NetEvent& event) { return event.type == type; });
    }

    LoopbackNetwork network;
    std::unique_ptr<IDatagramTransport> hostPort;
    std::unique_ptr<IDatagramTransport> clientPort;
    std::unique_ptr<TappingTransport> hostTap;
    std::unique_ptr<TappingTransport> clientTap;
    StaticKeyTable keys;
    std::unique_ptr<Host> host;
    std::unique_ptr<Client> client;
    std::vector<NetEvent> hostEvents;
    std::vector<NetEvent> clientEvents;
};

// ------------------------------------------------------------------ tests

void TestByteIo() {
    std::vector<std::uint8_t> bytes;
    ByteWriter writer(bytes);
    writer.U8(0xABU);
    writer.U16(0xBEEFU);
    writer.U32(0xDEADBEEFU);
    writer.U64(0x0123456789ABCDEFULL);
    ByteReader reader(bytes);
    Require(reader.U8() == 0xABU && reader.U16() == 0xBEEFU && reader.U32() == 0xDEADBEEFU && reader.U64() == 0x0123456789ABCDEFULL,
        "values did not round trip");
    Require(reader.Ok() && reader.Remaining() == 0U, "the reader lost track of its position");
    Require(bytes[1] == 0xEFU && bytes[2] == 0xBEU, "values are not little endian");

    ByteReader shortReader(std::span<const std::uint8_t>(bytes.data(), 3U));
    static_cast<void>(shortReader.U16());
    Require(shortReader.Ok(), "a read that fits failed");
    Require(shortReader.U32() == 0U && !shortReader.Ok(), "a read past the end did not fail");
    Require(shortReader.U8() == 0U && !shortReader.Ok(), "a failed reader recovered");
    ByteReader hugeReader(bytes);
    Require(hugeReader.Bytes(~std::size_t{ 0 }).empty() && !hugeReader.Ok(), "a huge length was accepted");
}

void TestConfigValidation() {
    Require(SessionConfig{}.IsValid(), "the default configuration is invalid");
    SessionConfig config;
    config.channels.clear();
    Require(!config.IsValid(), "a configuration without channels is valid");
    config = SessionConfig{};
    config.channels.assign(9U, ChannelMode::ReliableOrdered);
    Require(!config.IsValid(), "nine channels are valid");
    config = SessionConfig{};
    config.budget.packetBytes = 64U;
    Require(!config.IsValid(), "a packet size that cannot hold a fragment is valid");
    config = SessionConfig{};
    config.maxMessageBytes = 10U * 1024U * 1024U;
    Require(!config.IsValid(), "a message limit above the fragment limit is valid");
    config = SessionConfig{};
    config.budget.maxQueuedBytes = config.maxMessageBytes - 1U;
    Require(!config.IsValid(), "a queue smaller than one message is valid");
    config = SessionConfig{};
    config.timeoutMilliseconds = config.keepAliveMilliseconds;
    Require(!config.IsValid(), "a timeout that is not longer than the keep-alive is valid");
}

void TestBasicExchange() {
    SessionPair pair;
    const std::vector<std::uint8_t> reliable = Pattern(1U, 100U);
    const std::vector<std::uint8_t> unreliable = Pattern(2U, 50U);
    Require(pair.a.Send(0U, reliable) == SendResult::Queued, "reliable send refused");
    Require(pair.a.Send(1U, unreliable) == SendResult::Queued, "unreliable send refused");
    Exchange(pair.a, pair.b, 10U);
    Message message;
    bool gotReliable = false;
    bool gotUnreliable = false;
    while (pair.b.PopMessage(message)) {
        gotReliable = gotReliable || (message.channel == 0U && message.data == reliable);
        gotUnreliable = gotUnreliable || (message.channel == 1U && message.data == unreliable);
    }
    Require(gotReliable && gotUnreliable, "a message did not arrive");

    // The acknowledgement flows back and frees the send queue.
    Require(pair.a.QueuedSendBytes() == reliable.size(), "the queue lost the message before it was acknowledged");
    Exchange(pair.b, pair.a, 20U);
    Require(pair.a.QueuedSendBytes() == 0U, "an acknowledged message stayed queued");
    Require(pair.a.Stats().smoothedRttMilliseconds == 10U, "the round trip time was not measured");
}

void TestFragmentation() {
    SessionPair pair;
    const std::vector<std::uint8_t> large = Pattern(7U, pair.a.Config().maxMessageBytes);
    Require(pair.a.Send(0U, large) == SendResult::Queued, "a message at the limit was refused");
    Require(pair.a.Send(0U, Pattern(8U, pair.a.Config().maxMessageBytes + 1U)) == SendResult::TooLarge, "an oversized message was queued");
    Require(pair.a.Send(1U, Pattern(9U, pair.a.Config().maxUnreliableBytes + 1U)) == SendResult::TooLarge, "an oversized unreliable message was queued");
    Require(pair.a.Send(5U, Pattern(9U, 10U)) == SendResult::InvalidChannel, "an unknown channel was accepted");
    Require(pair.a.Send(0U, std::span<const std::uint8_t>{}) == SendResult::Queued, "an empty message was refused");

    std::uint64_t now = 0U;
    std::vector<Message> received;
    for (int round = 0; round < 200 && received.size() < 2U; ++round) {
        now += 10U;
        Exchange(pair.a, pair.b, now);
        Exchange(pair.b, pair.a, now);
        Message message;
        while (pair.b.PopMessage(message)) {
            received.push_back(std::move(message));
        }
    }
    Require(received.size() == 2U, "the fragmented message and the empty one did not both arrive");
    Require(received[0].data == large, "the reassembled message differs from the original");
    Require(received[1].data.empty(), "the empty message arrived with data");
    Require(pair.a.Stats().fragmentsSent >= 14U, "the large message was not fragmented");
}

void TestReplayAndWindow() {
    SessionPair pair;
    std::vector<std::vector<std::uint8_t>> packets;
    for (std::uint32_t index = 0U; index < 100U; ++index) {
        static_cast<void>(pair.a.Send(1U, Pattern(index, 10U)));
        const auto packet = pair.a.BuildPacket(index);
        Require(packet.has_value(), "no packet was built");
        packets.push_back(*packet);
    }
    // Newest first, then the rest in descending order. Packets more than 64 behind the newest are too old.
    std::size_t accepted = 0U;
    std::size_t replays = 0U;
    for (std::size_t index = packets.size(); index-- > 0U;) {
        const PacketResult result = pair.b.ProcessPacket(1000U, packets[index]);
        accepted += result == PacketResult::Accepted ? 1U : 0U;
        replays += result == PacketResult::Replay ? 1U : 0U;
    }
    Require(accepted == 64U && replays == 36U, "the replay window accepted " + std::to_string(accepted) + " packets");
    Require(pair.b.ProcessPacket(1001U, packets[99]) == PacketResult::Replay, "a duplicate was accepted");
    Require(pair.b.ProcessPacket(1001U, packets[50]) == PacketResult::Replay, "a duplicate inside the window was accepted");

    // Messages in the unreliable channel are sequenced: of the packets received in descending order
    // only the first (newest) message is delivered, the older ones are stale.
    Message message;
    std::size_t delivered = 0U;
    while (pair.b.PopMessage(message)) {
        ++delivered;
    }
    Require(delivered == 1U && pair.b.Stats().staleUnreliable == 63U, "stale unreliable messages were delivered");
}

void TestTamperedPackets() {
    SessionPair pair;
    static_cast<void>(pair.a.Send(0U, Pattern(1U, 200U)));
    const auto packet = pair.a.BuildPacket(10U);
    Require(packet.has_value(), "no packet was built");
    for (std::size_t index = 0U; index < packet->size(); ++index) {
        std::vector<std::uint8_t> copy = *packet;
        copy[index] ^= 0x01U;
        const PacketResult result = pair.b.ProcessPacket(10U, copy);
        Require(result != PacketResult::Accepted, "a packet with byte " + std::to_string(index) + " changed was accepted");
    }
    // A packet with changed bits did not use up its packet number.
    Require(pair.b.ProcessPacket(10U, *packet) == PacketResult::Accepted, "the genuine packet was refused after tampering");
    Require(pair.b.Stats().rejectedAuth > 0U && pair.b.Stats().rejectedMalformed > 0U, "rejections were not counted");

    // The same bytes under a different key are rejected.
    Session stranger(SessionConfig{}, MakeKey(5U), MakeKey(6U), 0U);
    Require(stranger.ProcessPacket(10U, *packet) == PacketResult::AuthFailed, "a packet under the wrong key was accepted");
    // Truncated and oversized input.
    Require(pair.b.ProcessPacket(10U, std::span<const std::uint8_t>(packet->data(), 20U)) == PacketResult::Malformed, "a truncated packet was accepted");
    Require(pair.b.ProcessPacket(10U, std::span<const std::uint8_t>{}) == PacketResult::Malformed, "an empty packet was accepted");
    std::vector<std::uint8_t> oversized(kMaxDatagramBytes + 1U, 0x4EU);
    Require(pair.b.ProcessPacket(10U, oversized) == PacketResult::Malformed, "an oversized packet was accepted");
}

void TestBackpressureLosesNothing() {
    SessionConfig receiverConfig;
    receiverConfig.maxInboxMessages = 4U;
    Session sender(SessionConfig{}, MakeKey(1U), MakeKey(2U), 0U);
    Session receiver(receiverConfig, MakeKey(2U), MakeKey(1U), 0U);

    constexpr std::uint32_t kTotal = 100U;
    std::uint32_t nextToSend = 0U;
    std::uint32_t nextExpected = 0U;
    std::uint64_t now = 0U;
    for (int round = 0; round < 3000 && nextExpected < kTotal; ++round) {
        now += 10U;
        while (nextToSend < kTotal && sender.Send(0U, Pattern(nextToSend, 2000U)) == SendResult::Queued) {
            ++nextToSend;
        }
        Exchange(sender, receiver, now);
        Exchange(receiver, sender, now);
        if (round >= 60) { // the application stops reading for a while at the start
            Message message;
            while (receiver.PopMessage(message)) {
                Require(IdOf(message.data) == nextExpected, "messages arrived out of order or twice");
                Require(message.data == Pattern(nextExpected, 2000U), "a message arrived damaged");
                ++nextExpected;
            }
        }
    }
    Require(nextExpected == kTotal, "only " + std::to_string(nextExpected) + " of 100 messages arrived");
    Require(receiver.Stats().rejectedBusy > 0U, "the full inbox never pushed back");
    for (int round = 0; round < 20; ++round) {
        now += 10U;
        Exchange(receiver, sender, now);
        Exchange(sender, receiver, now);
    }
    Require(sender.QueuedSendBytes() < 2000U * 5U, "the send queue did not drain");
}

void TestQueueLimits() {
    Session session(SessionConfig{}, MakeKey(1U), MakeKey(2U), 0U);
    const std::size_t limit = session.Config().budget.maxQueuedBytes;
    std::size_t queued = 0U;
    SendResult result = SendResult::Queued;
    while (result == SendResult::Queued && queued < 100U) {
        result = session.Send(0U, Pattern(1U, 4000U));
        queued += result == SendResult::Queued ? 1U : 0U;
    }
    Require(result == SendResult::Busy, "the send queue never pushed back");
    Require(session.QueuedSendBytes() <= limit, "the send queue grew past its budget");
    session.Disconnect();
    Require(session.Send(0U, Pattern(1U, 4U)) == SendResult::Closed, "a disconnecting session accepted a message");
}

LinkConditions BadNetwork() {
    LinkConditions conditions;
    conditions.simulation.latencyMilliseconds = 20U;
    conditions.simulation.jitterMilliseconds = 10U;
    conditions.simulation.lossPermille = 300U;
    conditions.simulation.reorderPermille = 200U;
    conditions.simulation.seed = 7U;
    conditions.duplicatePermille = 150U;
    return conditions;
}

void TestBadNetwork() {
    ClientConfig clientConfig;
    clientConfig.connectTimeoutMilliseconds = 30'000U;
    Rig rig(BadNetwork(), HostConfig{}, clientConfig);
    std::uint64_t now = rig.Run(0U, 30'000U, [&] { return rig.client->State() == ClientState::Connected && rig.host->ClientCount() == 1U; });
    Require(rig.client->State() == ClientState::Connected, "the client did not connect over a lossy network");
    Require(Rig::HasEvent(rig.hostEvents, NetEventType::Connected) && Rig::HasEvent(rig.clientEvents, NetEventType::Connected), "connect events are missing");

    constexpr std::uint32_t kReliable = 200U;
    constexpr std::uint32_t kUnreliable = 300U;
    const auto size = [](std::uint32_t id) { return id % 7U == 0U ? std::size_t{ 3000U } : std::size_t{ 20U } + id; };
    std::uint32_t clientNext = 0U;
    std::uint32_t hostNext = 0U;
    std::uint32_t clientUnreliableNext = 0U;
    std::uint32_t hostReceivedReliable = 0U;
    std::uint32_t clientReceivedReliable = 0U;
    std::int64_t lastHostUnreliable = -1;
    std::uint32_t hostReceivedUnreliable = 0U;
    const std::uint64_t start = now;
    for (; now < start + 120'000U; now += 10U) {
        while (clientNext < kReliable && rig.client->Send(0U, Pattern(clientNext, size(clientNext))) == SendResult::Queued) {
            ++clientNext;
        }
        while (hostNext < kReliable && rig.host->Send(42U, 0U, Pattern(hostNext, size(hostNext))) == SendResult::Queued) {
            ++hostNext;
        }
        if (clientUnreliableNext < kUnreliable && now % 20U == 0U) {
            static_cast<void>(rig.client->Send(1U, Pattern(clientUnreliableNext++, 40U)));
        }
        rig.Step(now);
        for (const NetEvent& event : rig.hostEvents) {
            if (event.type != NetEventType::Message) {
                continue;
            }
            if (event.message.channel == 0U) {
                Require(IdOf(event.message.data) == hostReceivedReliable, "the host got reliable message " + std::to_string(IdOf(event.message.data)) +
                    " when " + std::to_string(hostReceivedReliable) + " was next");
                Require(event.message.data == Pattern(hostReceivedReliable, size(hostReceivedReliable)), "a reliable message arrived damaged");
                ++hostReceivedReliable;
            } else {
                const std::int64_t id = IdOf(event.message.data);
                Require(id > lastHostUnreliable, "an unreliable message arrived twice or out of order");
                lastHostUnreliable = id;
                ++hostReceivedUnreliable;
            }
        }
        rig.hostEvents.clear();
        for (const NetEvent& event : rig.clientEvents) {
            if (event.type == NetEventType::Message && event.message.channel == 0U) {
                Require(IdOf(event.message.data) == clientReceivedReliable, "the client got a reliable message out of order");
                Require(event.message.data == Pattern(clientReceivedReliable, size(clientReceivedReliable)), "a reliable message arrived damaged");
                ++clientReceivedReliable;
            }
        }
        rig.clientEvents.clear();
        if (hostReceivedReliable == kReliable && clientReceivedReliable == kReliable && clientUnreliableNext == kUnreliable) {
            break;
        }
    }
    Require(hostReceivedReliable == kReliable && clientReceivedReliable == kReliable,
        "reliable delivery was incomplete: host " + std::to_string(hostReceivedReliable) + ", client " + std::to_string(clientReceivedReliable));
    Require(hostReceivedUnreliable > 0U && hostReceivedUnreliable < kUnreliable, "unreliable delivery looks wrong: " + std::to_string(hostReceivedUnreliable));

    const LoopbackStats& link = rig.network.Stats();
    Require(link.dropped > 0U && link.duplicated > 0U && link.reordered > 0U, "the simulated network did not misbehave");
    const SessionStats* hostSide = rig.host->ClientStats(42U);
    Require(hostSide != nullptr, "no statistics for the client");
    Require(rig.client->Stats()->fragmentsRetransmitted > 0U && hostSide->fragmentsRetransmitted > 0U, "nothing was retransmitted");
    Require(hostSide->rejectedReplay + rig.client->Stats()->rejectedReplay > 0U, "duplicated packets were not detected");
}

void TestHandshakeAuthentication() {
    // An attacker who does not know the key can obtain a Challenge, but a Response with a made-up MAC
    // is refused, whatever cookie it carries.
    {
        Rig rig;
        auto attacker = rig.network.CreateTransport(Endpoint::Loopback(7500));
        std::vector<std::uint8_t> connect{ 0x4EU, 0x4BU, 1U, 1U };
        ByteWriter connectWriter(connect);
        connectWriter.U64(42U);
        connectWriter.U64(0x1122334455667788ULL);
        connectWriter.Zeros(kMinConnectBytes - connect.size());
        Require(attacker->Send(Endpoint::Loopback(7000), connect) == TransportStatus::Ok, "the attacker could not send");
        rig.network.Advance(1U);
        rig.host->Update(1U);
        rig.network.Advance(2U);
        std::vector<std::uint8_t> challenge(kMaxDatagramBytes + 1U);
        Endpoint from;
        std::size_t size = 0U;
        Require(attacker->Receive(from, challenge, size) == TransportStatus::Ok && size == 52U, "no Challenge was sent");
        Require(size < connect.size(), "the Challenge is not smaller than the Connect");
        std::vector<std::uint8_t> response{ 0x4EU, 0x4BU, 1U, 3U };
        ByteWriter responseWriter(response);
        responseWriter.U64(42U);
        responseWriter.U64(0x1122334455667788ULL);
        responseWriter.Bytes(std::span<const std::uint8_t>(challenge.data() + 12U, 8U)); // the host's cookie
        responseWriter.Bytes(MakeKey(200U));                                             // a MAC the attacker invented
        Require(attacker->Send(Endpoint::Loopback(7000), response) == TransportStatus::Ok, "the attacker could not send");
        rig.network.Advance(3U);
        rig.host->Update(3U);
        Require(rig.host->Stats().handshakesRejected == 1U && rig.host->ClientCount() == 0U, "a Response with an invented MAC created a session");
    }
    // A client with the wrong key never connects, and the host stores nothing for it.
    {
        ClientConfig clientConfig;
        clientConfig.connectTimeoutMilliseconds = 2000U;
        Rig rig({}, {}, clientConfig, 42U, MakeKey(77U));
        rig.Run(0U, 3000U, [&] { return rig.client->State() == ClientState::Disconnected; });
        Require(rig.client->State() == ClientState::Disconnected, "a client with the wrong key stayed connecting");
        Require(!rig.clientEvents.empty() && rig.clientEvents.back().reason == DisconnectReason::ConnectTimeout, "the wrong key did not end in a connect timeout");
        Require(rig.host->ClientCount() == 0U && rig.host->Stats().handshakesCompleted == 0U, "the host accepted a client with the wrong key");
    }
    // An unknown client id is ignored.
    {
        ClientConfig clientConfig;
        clientConfig.connectTimeoutMilliseconds = 1000U;
        Rig rig({}, {}, clientConfig, 999U, MakeKey(9U));
        rig.Run(0U, 2000U, [&] { return rig.client->State() == ClientState::Disconnected; });
        Require(rig.client->State() == ClientState::Disconnected && rig.host->ClientCount() == 0U, "an unknown client id connected");
        Require(rig.host->Stats().handshakesRejected > 0U && rig.host->Stats().challengesSent == 0U, "an unknown client id received a challenge");
    }
    // A genuine client connects, and its Response cannot be replayed from another address.
    {
        Rig rig;
        rig.Run(0U, 2000U, [&] { return rig.client->State() == ClientState::Connected; });
        Require(rig.client->State() == ClientState::Connected && rig.host->ClientCount() == 1U, "the client did not connect");
        std::vector<std::uint8_t> response;
        std::vector<std::uint8_t> connect;
        for (const std::vector<std::uint8_t>& packet : rig.clientTap->sent) {
            if (packet.size() >= 4U && packet[3] == 0x03U) {
                response = packet;
            }
            if (packet.size() >= 4U && packet[3] == 0x01U) {
                connect = packet;
            }
        }
        Require(!response.empty() && !connect.empty(), "the handshake packets were not seen");
        const std::uint64_t rejectedBefore = rig.host->Stats().handshakesRejected;
        rig.network.Inject(Endpoint::Loopback(7777), Endpoint::Loopback(7000), response);
        rig.Step(2010U);
        Require(rig.host->Stats().handshakesRejected == rejectedBefore + 1U, "a Response from another address was accepted");
        Require(rig.host->ClientCount() == 1U, "the replayed Response changed the connection");
        // A repeated Response from the real address is harmless.
        rig.network.Inject(Endpoint::Loopback(7001), Endpoint::Loopback(7000), response);
        rig.Step(2020U);
        Require(rig.host->ClientCount() == 1U && rig.host->Stats().handshakesCompleted == 1U, "a repeated Response created a second session");
        // A Connect is padded, so the Challenge cannot amplify.
        Require(connect.size() >= kMinConnectBytes, "the Connect is not padded");
        // A short Connect is refused without a reply.
        const std::uint64_t challenges = rig.host->Stats().challengesSent;
        rig.network.Inject(Endpoint::Loopback(7778), Endpoint::Loopback(7000), std::span<const std::uint8_t>(connect.data(), 40U));
        rig.Step(2030U);
        Require(rig.host->Stats().challengesSent == challenges, "a short Connect received a Challenge");
    }
}

void TestLimitsAndFlood() {
    HostConfig hostConfig;
    hostConfig.session.limits.maxMessagesPerTick = 8U;
    Rig rig({}, hostConfig);
    rig.Run(0U, 2000U, [&] { return rig.client->State() == ClientState::Connected; });
    Require(rig.client->State() == ClientState::Connected, "the client did not connect");

    // An oversized datagram is counted and dropped.
    const std::vector<std::uint8_t> oversized(kMaxDatagramBytes + 1U, 0x4EU);
    rig.network.Inject(Endpoint::Loopback(7001), Endpoint::Loopback(7000), oversized);
    rig.Step(2010U);
    Require(rig.host->Stats().oversized == 1U, "an oversized datagram was not counted");

    // A flood of datagrams from one peer is cut to the per-update limit.
    static_cast<void>(rig.client->Send(0U, Pattern(1U, 20U)));
    rig.Step(2020U);
    std::vector<std::uint8_t> genuine;
    for (auto packet = rig.clientTap->sent.rbegin(); packet != rig.clientTap->sent.rend(); ++packet) {
        if (packet->size() > 4U && (*packet)[3] == 0x10U) {
            genuine = *packet;
            break;
        }
    }
    Require(!genuine.empty(), "no data packet was seen");
    for (int copy = 0; copy < 100; ++copy) {
        rig.network.Inject(Endpoint::Loopback(7001), Endpoint::Loopback(7000), genuine);
    }
    rig.Step(2030U);
    Require(rig.host->Stats().rateLimited >= 90U, "the flood was not limited: " + std::to_string(rig.host->Stats().rateLimited));
    Require(rig.host->ClientStats(42U)->rejectedReplay >= 1U, "copies of a packet were not rejected as replays");
    // Sealed data from an address that has no session is dropped.
    rig.network.Inject(Endpoint::Loopback(7999), Endpoint::Loopback(7000), genuine);
    rig.Step(2040U);
    Require(rig.host->Stats().unknownPeer == 1U, "data from an unknown address was not dropped");
    // The real connection still works.
    rig.Run(2050U, 4000U, [&] { return false; });
    Require(rig.client->State() == ClientState::Connected && rig.host->ClientCount() == 1U, "the flood broke the connection");
}

std::vector<std::uint8_t> RandomBytes(Random& random, std::size_t size) {
    std::vector<std::uint8_t> bytes(size);
    for (std::uint8_t& byte : bytes) {
        byte = static_cast<std::uint8_t>(random.Next() & 0xFFU);
    }
    return bytes;
}

std::vector<std::uint8_t> Mutate(Random& random, std::vector<std::uint8_t> bytes) {
    switch (random.Below(5U)) {
    case 0U: {
        const std::size_t flips = 1U + random.Below(3U);
        for (std::size_t flip = 0U; flip < flips && !bytes.empty(); ++flip) {
            bytes[random.Below(bytes.size())] ^= static_cast<std::uint8_t>(1U << random.Below(8U));
        }
        break;
    }
    case 1U:
        bytes.resize(random.Below(bytes.size() + 1U));
        break;
    case 2U: {
        const std::vector<std::uint8_t> extra = RandomBytes(random, 1U + random.Below(64U));
        bytes.insert(bytes.end(), extra.begin(), extra.end());
        break;
    }
    case 3U:
        if (!bytes.empty()) {
            bytes[random.Below(bytes.size())] = static_cast<std::uint8_t>(random.Next() & 0xFFU);
        }
        break;
    default:
        break; // an exact copy
    }
    return bytes;
}

void TestHostileInput() {
    Rig rig;
    rig.Run(0U, 2000U, [&] { return rig.client->State() == ClientState::Connected; });
    Require(rig.client->State() == ClientState::Connected, "the client did not connect");
    for (int index = 0; index < 20; ++index) {
        static_cast<void>(rig.client->Send(0U, Pattern(static_cast<std::uint32_t>(index), 600U)));
        static_cast<void>(rig.host->Send(42U, 0U, Pattern(static_cast<std::uint32_t>(index), 600U)));
        static_cast<void>(rig.client->Send(1U, Pattern(static_cast<std::uint32_t>(index), 40U)));
    }
    std::uint64_t now = 2000U;
    rig.Run(now, now + 500U, [&] { return false; });
    now += 500U;

    const Endpoint hostEndpoint = Endpoint::Loopback(7000);
    const Endpoint clientEndpoint = Endpoint::Loopback(7001);
    Random random{ 0x9E3779B97F4A7C15ULL };
    std::size_t injected = 0U;
    for (std::size_t iteration = 0U; iteration < 20'000U; ++iteration) {
        const std::size_t mode = random.Below(6U);
        std::vector<std::uint8_t> bytes;
        Endpoint from = clientEndpoint;
        Endpoint to = hostEndpoint;
        if (mode == 0U) {
            bytes = RandomBytes(random, random.Below(1300U));
        } else if (mode == 1U) {
            bytes = RandomBytes(random, random.Below(1300U));
            from = hostEndpoint;
            to = clientEndpoint;
        } else if (mode == 2U && !rig.clientTap->sent.empty()) {
            bytes = Mutate(random, rig.clientTap->sent[random.Below(rig.clientTap->sent.size())]);
        } else if (mode == 3U && !rig.hostTap->sent.empty()) {
            bytes = Mutate(random, rig.hostTap->sent[random.Below(rig.hostTap->sent.size())]);
            from = hostEndpoint;
            to = clientEndpoint;
        } else if (mode == 4U && !rig.clientTap->sent.empty()) {
            bytes = Mutate(random, rig.clientTap->sent[random.Below(std::min<std::size_t>(rig.clientTap->sent.size(), 4U))]);
            from = Endpoint::Loopback(static_cast<std::uint16_t>(8000U + random.Below(50U)));
        } else {
            // A header with plausible fields and random content, from the real address.
            bytes = RandomBytes(random, 4U + random.Below(300U));
            bytes[0] = 0x4EU;
            bytes[1] = 0x4BU;
            bytes[2] = 1U;
            bytes[3] = random.Below(2U) == 0U ? 0x10U : static_cast<std::uint8_t>(1U + random.Below(3U));
        }
        rig.network.Inject(from, to, bytes);
        ++injected;
        if (iteration % 50U == 49U) {
            now += 10U;
            rig.Step(now);
        }
    }
    Require(injected == 20'000U, "the fuzz loop did not run");
    Require(rig.host->Stats().malformed + rig.host->Stats().handshakesRejected + rig.host->Stats().unknownPeer + rig.host->Stats().oversized > 0U,
        "no hostile datagram was rejected by the host");

    // Nothing hostile became a message, and the connection is intact and still carries data.
    rig.hostEvents.clear();
    rig.clientEvents.clear();
    Require(rig.client->State() == ClientState::Connected && rig.host->ClientCount() == 1U, "hostile input broke the connection");
    static_cast<void>(rig.client->Send(0U, Pattern(5000U, 100U)));
    static_cast<void>(rig.host->Send(42U, 0U, Pattern(6000U, 100U)));
    bool hostGot = false;
    bool clientGot = false;
    for (int step = 0; step < 300 && !(hostGot && clientGot); ++step) {
        now += 10U;
        rig.Step(now);
        for (const NetEvent& event : rig.hostEvents) {
            hostGot = hostGot || (event.type == NetEventType::Message && event.message.channel == 0U && IdOf(event.message.data) == 5000U);
        }
        for (const NetEvent& event : rig.clientEvents) {
            clientGot = clientGot || (event.type == NetEventType::Message && event.message.channel == 0U && IdOf(event.message.data) == 6000U);
        }
        rig.hostEvents.clear();
        rig.clientEvents.clear();
    }
    Require(hostGot && clientGot, "the connection stopped carrying data after hostile input");
    Require(rig.client->State() == ClientState::Connected, "the client dropped after hostile input");
}

void TestHostileInputIntoSession() {
    // The same bytes straight into a session, with no transport in between.
    SessionPair pair;
    static_cast<void>(pair.a.Send(0U, Pattern(1U, 2000U)));
    std::vector<std::vector<std::uint8_t>> genuine;
    for (int index = 0; index < 8; ++index) {
        const auto packet = pair.a.BuildPacket(static_cast<std::uint64_t>(index) * 300U);
        if (packet.has_value()) {
            genuine.push_back(*packet);
        }
    }
    Require(!genuine.empty(), "no packets were built");
    Random random{ 0xD1B54A32D192ED03ULL };
    for (std::size_t iteration = 0U; iteration < 50'000U; ++iteration) {
        std::vector<std::uint8_t> bytes = iteration % 2U == 0U ? RandomBytes(random, random.Below(1300U))
                                                               : Mutate(random, genuine[random.Below(genuine.size())]);
        const PacketResult result = pair.b.ProcessPacket(5000U, bytes);
        // Only an unmodified genuine packet may be accepted; everything else is rejected.
        if (result == PacketResult::Accepted) {
            Require(std::ranges::any_of(genuine, [&](const std::vector<std::uint8_t>& packet) { return packet == bytes; }),
                "a modified packet was accepted");
        }
    }
    // The genuine message still arrives.
    std::uint64_t now = 6000U;
    Message message;
    bool arrived = false;
    for (int round = 0; round < 300 && !arrived; ++round) {
        now += 10U;
        Exchange(pair.a, pair.b, now);
        Exchange(pair.b, pair.a, now);
        arrived = pair.b.PopMessage(message) && message.data == Pattern(1U, 2000U);
    }
    Require(arrived, "the message did not arrive after hostile input");
}

void TestDisconnectAndTimeout() {
    {
        Rig rig;
        std::uint64_t now = rig.Run(0U, 2000U, [&] { return rig.client->State() == ClientState::Connected; });
        rig.client->Disconnect();
        rig.Run(now, now + 2000U, [&] { return rig.client->State() == ClientState::Disconnected && rig.host->ClientCount() == 0U; });
        Require(rig.client->State() == ClientState::Disconnected && rig.host->ClientCount() == 0U, "a requested disconnect did not complete");
        const auto hostDisconnect = std::ranges::find_if(rig.hostEvents, [](const NetEvent& event) { return event.type == NetEventType::Disconnected; });
        Require(hostDisconnect != rig.hostEvents.end() && hostDisconnect->reason == DisconnectReason::PeerClosed && hostDisconnect->peer == 42U,
            "the host was not told the client left");
        Require(rig.clientEvents.back().reason == DisconnectReason::Requested, "the client did not report its own disconnect");
    }
    {
        Rig rig;
        std::uint64_t now = rig.Run(0U, 2000U, [&] { return rig.client->State() == ClientState::Connected; });
        // The link goes silent: both sides time out.
        LinkConditions cut;
        cut.simulation.disconnectAtTick = now;
        rig.network.SetConditions(cut);
        rig.Run(now, now + 15'000U, [&] { return rig.client->State() == ClientState::Disconnected && rig.host->ClientCount() == 0U; });
        Require(rig.client->State() == ClientState::Disconnected && rig.host->ClientCount() == 0U, "a silent link did not time out");
        const auto hostDisconnect = std::ranges::find_if(rig.hostEvents, [](const NetEvent& event) { return event.type == NetEventType::Disconnected; });
        Require(hostDisconnect != rig.hostEvents.end() && hostDisconnect->reason == DisconnectReason::Timeout, "the host did not report a timeout");
        Require(rig.clientEvents.back().reason == DisconnectReason::Timeout, "the client did not report a timeout");
    }
    {
        // The same client id connecting from another address replaces the first connection.
        Rig rig;
        rig.Run(0U, 2000U, [&] { return rig.client->State() == ClientState::Connected; });
        auto secondPort = rig.network.CreateTransport(Endpoint::Loopback(7002));
        Client second(*secondPort, ClientConfig{}, 42U, MakeKey(9U), Endpoint::Loopback(7000), 2000U);
        for (std::uint64_t now = 2000U; now < 3000U; now += 10U) {
            rig.Step(now);
            second.Update(now);
        }
        Require(second.State() == ClientState::Connected && rig.host->ClientCount() == 1U, "the second connection did not replace the first");
        const bool replaced = std::ranges::any_of(rig.hostEvents, [](const NetEvent& event) {
            return event.type == NetEventType::Disconnected && event.reason == DisconnectReason::Replaced;
        });
        Require(replaced, "the replaced connection was not reported");
    }
}

void TestHostCapacity() {
    HostConfig hostConfig;
    hostConfig.maxClients = 1U;
    Rig rig({}, hostConfig);
    rig.keys.Set(43U, MakeKey(10U));
    rig.Run(0U, 2000U, [&] { return rig.client->State() == ClientState::Connected; });
    auto port = rig.network.CreateTransport(Endpoint::Loopback(7003));
    ClientConfig config;
    config.connectTimeoutMilliseconds = 1500U;
    Client other(*port, config, 43U, MakeKey(10U), Endpoint::Loopback(7000), 2000U);
    for (std::uint64_t now = 2000U; now < 4000U; now += 10U) {
        rig.Step(now);
        other.Update(now);
    }
    Require(other.State() == ClientState::Disconnected && rig.host->ClientCount() == 1U, "a client was accepted beyond the limit");
    Require(rig.client->State() == ClientState::Connected, "the connected client was disturbed");
}

void TestRealUdp() {
    std::string error;
    auto hostSocket = CreateUdpTransport(Endpoint::Loopback(0U), error);
    if (hostSocket == nullptr) {
        std::printf("  skipped: no UDP socket (%s)\n", error.c_str());
        return;
    }
    auto clientSocket = CreateUdpTransport(Endpoint::Loopback(0U), error);
    Require(clientSocket != nullptr, "the second socket could not be created: " + error);
    Require(hostSocket->LocalEndpoint().port != 0U && hostSocket->LocalEndpoint().port != clientSocket->LocalEndpoint().port, "the sockets did not get distinct ports");

    StaticKeyTable keys;
    keys.Set(7U, MakeKey(3U));
    Host host(*hostSocket, HostConfig{}, keys);
    const auto start = std::chrono::steady_clock::now();
    const auto elapsed = [&] {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
    };
    Client client(*clientSocket, ClientConfig{}, 7U, MakeKey(3U), Endpoint::Loopback(hostSocket->LocalEndpoint().port), elapsed());

    const std::vector<std::uint8_t> large = Pattern(77U, 9000U);
    bool sent = false;
    bool hostGot = false;
    bool clientGot = false;
    while (elapsed() < 5000U && !(hostGot && clientGot)) {
        const std::uint64_t now = elapsed();
        host.Update(now);
        client.Update(now);
        NetEvent event;
        while (host.PollEvent(event)) {
            if (event.type == NetEventType::Message && event.message.data == large) {
                hostGot = true;
                static_cast<void>(host.Send(7U, 0U, large));
            }
        }
        while (client.PollEvent(event)) {
            clientGot = clientGot || (event.type == NetEventType::Message && event.message.data == large);
        }
        if (!sent && client.State() == ClientState::Connected) {
            sent = client.Send(0U, large) == SendResult::Queued;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    Require(client.State() == ClientState::Connected, "the client did not connect over UDP");
    Require(hostGot && clientGot, "the 9000 byte message did not make the round trip over UDP");

    // A datagram larger than the limit is refused before it reaches the socket.
    const std::vector<std::uint8_t> oversized(kMaxDatagramBytes + 1U, 0U);
    Require(clientSocket->Send(hostSocket->LocalEndpoint(), oversized) == TransportStatus::TooLarge, "an oversized datagram was sent");
    Endpoint from;
    std::size_t size = 0U;
    std::vector<std::uint8_t> buffer(kMaxDatagramBytes + 1U);
    for (int attempt = 0; attempt < 50; ++attempt) {
        if (hostSocket->Receive(from, buffer, size) == TransportStatus::WouldBlock) {
            break;
        }
    }
    Require(hostSocket->Receive(from, buffer, size) == TransportStatus::WouldBlock, "an empty socket did not report WouldBlock");
}

void TestLoopbackNetwork() {
    LoopbackNetwork network;
    auto first = network.CreateTransport(Endpoint::Loopback(5000U));
    Require(first != nullptr && network.CreateTransport(Endpoint::Loopback(5000U)) == nullptr, "a port was handed out twice");
    Require(network.CreateTransport(Endpoint::Loopback(0U)) == nullptr, "port 0 was accepted");
    auto second = network.CreateTransport(Endpoint::Loopback(5001U));
    const std::vector<std::uint8_t> data{ 1U, 2U, 3U };
    Require(first->Send(second->LocalEndpoint(), data) == TransportStatus::Ok, "send failed");
    Endpoint from;
    std::size_t size = 0U;
    std::vector<std::uint8_t> buffer(16U);
    Require(second->Receive(from, buffer, size) == TransportStatus::WouldBlock, "a datagram arrived before time advanced");
    network.Advance(1U);
    Require(second->Receive(from, buffer, size) == TransportStatus::Ok && size == 3U && from == first->LocalEndpoint(), "the datagram did not arrive intact");
    Require(first->Send(second->LocalEndpoint(), std::vector<std::uint8_t>(kMaxDatagramBytes + 1U)) == TransportStatus::TooLarge, "an oversized datagram was sent");
    // The simulation is deterministic: the same seed drops the same packets.
    const auto dropped = [](std::uint64_t seed) {
        LinkConditions conditions;
        conditions.simulation.lossPermille = 500U;
        conditions.simulation.seed = seed;
        LoopbackNetwork net(conditions);
        auto a = net.CreateTransport(Endpoint::Loopback(1U));
        auto b = net.CreateTransport(Endpoint::Loopback(2U));
        for (std::uint8_t index = 0U; index < 200U; ++index) {
            static_cast<void>(a->Send(b->LocalEndpoint(), std::vector<std::uint8_t>{ index }));
            net.Advance(index);
        }
        return net.Stats().dropped;
    };
    Require(dropped(5U) == dropped(5U) && dropped(5U) > 40U && dropped(5U) < 160U, "the loss simulation is not deterministic or not near 50%");
}

} // namespace

int main() {
    const std::vector<std::pair<const char*, void (*)()>> tests{
        { "byte-io", &TestByteIo },
        { "config-validation", &TestConfigValidation },
        { "loopback-network", &TestLoopbackNetwork },
        { "basic-exchange", &TestBasicExchange },
        { "fragmentation", &TestFragmentation },
        { "replay-window", &TestReplayAndWindow },
        { "tampered-packets", &TestTamperedPackets },
        { "backpressure", &TestBackpressureLosesNothing },
        { "queue-limits", &TestQueueLimits },
        { "handshake-authentication", &TestHandshakeAuthentication },
        { "limits-and-flood", &TestLimitsAndFlood },
        { "disconnect-and-timeout", &TestDisconnectAndTimeout },
        { "host-capacity", &TestHostCapacity },
        { "hostile-input-session", &TestHostileInputIntoSession },
        { "hostile-input-host", &TestHostileInput },
        { "bad-network", &TestBadNetwork },
        { "real-udp", &TestRealUdp },
    };
    int failures = 0;
    for (const auto& [name, run] : tests) {
        std::printf("START %s\n", name);
        std::fflush(stdout);
        try {
            run();
            std::printf("PASS %s\n", name);
        } catch (const std::exception& error) {
            std::printf("FAIL %s: %s\n", name, error.what());
            ++failures;
        }
        std::fflush(stdout);
    }
    return failures == 0 ? 0 : 1;
}
