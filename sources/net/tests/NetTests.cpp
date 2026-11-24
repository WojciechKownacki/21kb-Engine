// Tests for kb_net: wire reading, sessions, the handshake, hostile input and a bad network.
// Every test is deterministic: time is passed in, the simulated network is seeded, and the fuzz
// generator is fixed.

#include "kb/net/ByteIO.hpp"
#include "kb/net/Host.hpp"
#include "kb/net/NetworkClock.hpp"
#include "kb/net/Prediction.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <functional>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <cstdlib>
#include <new>
#if defined(_WIN32)
#include <malloc.h>
#endif

namespace network_allocations {
thread_local bool enabled = false;
thread_local std::size_t count = 0U;
}
void* operator new(std::size_t bytes) {
    if (network_allocations::enabled) ++network_allocations::count;
    if (void* memory = std::malloc(bytes == 0U ? 1U : bytes)) return memory;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }
void* operator new(std::size_t bytes, std::align_val_t alignment) {
    if (network_allocations::enabled) ++network_allocations::count;
    void* memory = nullptr;
#if defined(_WIN32)
    memory = _aligned_malloc(bytes == 0U ? 1U : bytes, static_cast<std::size_t>(alignment));
#else
    if (posix_memalign(&memory, static_cast<std::size_t>(alignment), bytes == 0U ? 1U : bytes) != 0) memory = nullptr;
#endif
    if (memory != nullptr) return memory;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t bytes, std::align_val_t alignment) { return ::operator new(bytes, alignment); }
void operator delete(void* memory, std::align_val_t) noexcept {
#if defined(_WIN32)
    _aligned_free(memory);
#else
    std::free(memory);
#endif
}
void operator delete[](void* memory, std::align_val_t alignment) noexcept { ::operator delete(memory, alignment); }
void operator delete(void* memory, std::size_t, std::align_val_t alignment) noexcept { ::operator delete(memory, alignment); }
void operator delete[](void* memory, std::size_t, std::align_val_t alignment) noexcept { ::operator delete(memory, alignment); }

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
    bool WaitReadable(std::uint32_t milliseconds) override { return inner_->WaitReadable(milliseconds); }

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
    std::vector<std::uint8_t> packet;
    while (packets < limit) {
        if (!from.BuildPacket(now, packet)) {
            break;
        }
        static_cast<void>(to.ProcessPacket(now, packet));
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

void TestEveryRefusalIsNamedAndCounted() {
    DiagLog log;
    for (std::uint16_t code = 0U; code <= static_cast<std::uint16_t>(DiagCode::UnauthenticatedGuests); ++code) {
        log.Report(static_cast<DiagCode>(code), "entity=42 bytes=2048");
    }
    const auto codeCount = static_cast<std::size_t>(DiagCode::UnauthenticatedGuests) + 1U;
    Require(log.Total() == codeCount && log.Entries().size() == codeCount, "diagnostic codes were lost");
    for (std::size_t index = 0U; index < log.Entries().size(); ++index) {
        const DiagEntry& entry = log.Entries()[index];
        Require(static_cast<std::size_t>(entry.code) == index && entry.count == 1U &&
            std::string_view(entry.detail) == "entity=42 bytes=2048" && DiagName(entry.code) != "Unknown",
            "a refusal has no named code, count or detail");
    }
}

void TestDiagLogIsRateLimited() {
    DiagLog log;
    std::size_t lines = 0U;
    bool named = true;
    log.SetSink([&](std::string_view line) { ++lines; named = named && line.starts_with("EntityLimit:"); });
    for (std::size_t index = 0U; index < 1000U; ++index) log.Report(DiagCode::EntityLimit, "limit=8");
    Require(named && lines == 1U && log.Total() == 1000U && log.Entries()[0].count == 1000U, "diagnostics were not rate limited");
    log.SetSink([](std::string_view) { throw std::runtime_error("sink failure"); });
    log.Report(DiagCode::OversizeRecord, std::string(300U, 'x'));
    Require(std::string_view(log.Entries()[1].detail).size() == 159U, "detail is not bounded");
}

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
        std::vector<std::uint8_t> packet;
        Require(pair.a.BuildPacket(index, packet), "no packet was built");
        packets.push_back(packet);
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
    std::vector<std::uint8_t> packet;
    Require(pair.a.BuildPacket(10U, packet), "no packet was built");
    for (std::size_t index = 0U; index < packet.size(); ++index) {
        std::vector<std::uint8_t> copy = packet;
        copy[index] ^= 0x01U;
        const PacketResult result = pair.b.ProcessPacket(10U, copy);
        Require(result != PacketResult::Accepted, "a packet with byte " + std::to_string(index) + " changed was accepted");
    }
    // A packet with changed bits did not use up its packet number.
    Require(pair.b.ProcessPacket(10U, packet) == PacketResult::Accepted, "the genuine packet was refused after tampering");
    Require(pair.b.Stats().rejectedAuth > 0U && pair.b.Stats().rejectedMalformed > 0U, "rejections were not counted");

    // The same bytes under a different key are rejected.
    Session stranger(SessionConfig{}, MakeKey(5U), MakeKey(6U), 0U);
    Require(stranger.ProcessPacket(10U, packet) == PacketResult::AuthFailed, "a packet under the wrong key was accepted");
    // Truncated and oversized input.
    Require(pair.b.ProcessPacket(10U, std::span<const std::uint8_t>(packet.data(), 20U)) == PacketResult::Malformed, "a truncated packet was accepted");
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

void TestTwoClientsAndReconnectOnBadNetwork() {
    LoopbackNetwork network(BadNetwork());
    auto hostPort = network.CreateTransport(Endpoint::Loopback(7000));
    auto portA = network.CreateTransport(Endpoint::Loopback(7101));
    auto portB = network.CreateTransport(Endpoint::Loopback(7102));
    auto portA2 = network.CreateTransport(Endpoint::Loopback(7103)); // where client A comes back from
    StaticKeyTable keys;
    keys.Set(42U, MakeKey(1U));
    keys.Set(43U, MakeKey(2U));
    Host host(*hostPort, HostConfig{}, keys);
    ClientConfig config;
    config.connectTimeoutMilliseconds = 30'000U;
    auto clientA = std::make_unique<Client>(*portA, config, 42U, MakeKey(1U), Endpoint::Loopback(7000), 0U);
    Client clientB(*portB, config, 43U, MakeKey(2U), Endpoint::Loopback(7000), 0U);

    std::vector<std::vector<std::uint32_t>> received(2U); // per client, ids in arrival order
    std::vector<std::uint32_t> receivedByA;
    std::vector<std::uint32_t> receivedByB;
    std::uint32_t sentA = 0U;
    std::uint32_t sentB = 0U;
    std::uint32_t hostToB = 0U;
    bool reconnected = false;
    bool replacedSeen = false;
    std::uint64_t now = 0U;
    const auto drain = [&] {
        NetEvent event;
        while (host.PollEvent(event)) {
            if (event.type == NetEventType::Message) {
                received[event.peer == 42U ? 0U : 1U].push_back(IdOf(event.message.data));
            } else if (event.type == NetEventType::Disconnected && event.reason == DisconnectReason::Replaced) {
                replacedSeen = true;
            }
        }
        while (clientA != nullptr && clientA->PollEvent(event)) {
        }
        while (clientB.PollEvent(event)) {
            if (event.type == NetEventType::Message) {
                receivedByB.push_back(IdOf(event.message.data));
            }
        }
    };
    const auto step = [&] {
        network.Advance(now);
        host.Update(now);
        if (clientA != nullptr) {
            clientA->Update(now);
        }
        clientB.Update(now);
        drain();
    };

    for (; now < 30'000U && !(clientA->State() == ClientState::Connected && clientB.State() == ClientState::Connected); now += 10U) {
        step();
    }
    Require(clientA->State() == ClientState::Connected && clientB.State() == ClientState::Connected, "two clients did not connect over a bad network");

    constexpr std::uint32_t kPerClient = 60U;
    std::uint64_t reconnectAt = 0U;
    for (const std::uint64_t limit = now + 120'000U; now < limit; now += 10U) {
        // Client A sends the first half, then drops off and comes back from another address.
        while (clientA != nullptr && sentA < kPerClient / 2U && clientA->Send(0U, Pattern(sentA, 300U)) == SendResult::Queued) {
            ++sentA;
        }
        while (sentB < kPerClient && clientB.Send(0U, Pattern(sentB, 300U)) == SendResult::Queued) {
            ++sentB;
        }
        while (hostToB < kPerClient && host.Send(43U, 0U, Pattern(hostToB, 300U)) == SendResult::Queued) {
            ++hostToB;
        }
        if (clientA != nullptr && sentA == kPerClient / 2U && received[0].size() >= 10U) {
            clientA.reset(); // the process is gone: no goodbye, the old session is left to be replaced
            reconnectAt = now;
            clientA = std::make_unique<Client>(*portA2, config, 42U, MakeKey(1U), Endpoint::Loopback(7000), now);
            reconnected = true;
            sentA = 1000U;
        }
        if (reconnected && clientA->State() == ClientState::Connected && sentA >= 1000U && sentA < 1000U + kPerClient / 2U) {
            while (sentA < 1000U + kPerClient / 2U && clientA->Send(0U, Pattern(sentA, 300U)) == SendResult::Queued) {
                ++sentA;
            }
        }
        step();
        const bool aDone = reconnected && sentA == 1000U + kPerClient / 2U && received[0].size() >= 10U &&
            std::ranges::count_if(received[0], [](std::uint32_t id) { return id >= 1000U; }) == static_cast<std::ptrdiff_t>(kPerClient / 2U);
        if (aDone && received[1].size() == kPerClient && receivedByB.size() == kPerClient) {
            break;
        }
    }
    Require(reconnected && replacedSeen, "the reconnect of client A was not seen as a replacement");

    // Client B's streams are complete and in order both ways, whatever happened to A.
    Require(received[1].size() == kPerClient && receivedByB.size() == kPerClient, "client B lost messages while A reconnected");
    for (std::uint32_t index = 0U; index < kPerClient; ++index) {
        Require(received[1][index] == index && receivedByB[index] == index, "client B's messages are out of order");
    }
    // Client A: what arrived before the crash is an unbroken prefix, and what was sent after the
    // reconnect arrived complete and in order.
    std::uint32_t expectedBefore = 0U;
    std::uint32_t expectedAfter = 1000U;
    for (const std::uint32_t id : received[0]) {
        if (id < 1000U) {
            Require(id == expectedBefore++, "client A's messages before the crash have a gap or repeat");
        } else {
            Require(id == expectedAfter++, "client A's messages after the reconnect have a gap or repeat");
        }
    }
    Require(expectedAfter == 1000U + kPerClient / 2U, "client A's messages after the reconnect did not all arrive");
    Require(host.ClientCount() == 2U, "the host holds a stale session after the reconnect");

    // The report: what the bad network cost.
    const LoopbackStats& link = network.Stats();
    const SessionStats* statsB = host.ClientStats(43U);
    Require(statsB != nullptr && clientB.Stats() != nullptr, "no statistics");
    std::printf("  network report: simulated %llu ms, link sent %llu dropped %llu duplicated %llu reordered %llu; client B: rtt %u ms, %llu packets and %llu bytes sent,"
                " %llu fragments retransmitted, %llu duplicates and %llu replays rejected; all traffic completed %llu ms after the reconnect started\n",
        static_cast<unsigned long long>(now), static_cast<unsigned long long>(link.sent), static_cast<unsigned long long>(link.dropped),
        static_cast<unsigned long long>(link.duplicated), static_cast<unsigned long long>(link.reordered), clientB.Stats()->smoothedRttMilliseconds,
        static_cast<unsigned long long>(clientB.Stats()->packetsSent), static_cast<unsigned long long>(clientB.Stats()->bytesSent),
        static_cast<unsigned long long>(clientB.Stats()->fragmentsRetransmitted), static_cast<unsigned long long>(statsB->duplicateFragments),
        static_cast<unsigned long long>(statsB->rejectedReplay), static_cast<unsigned long long>(now - reconnectAt));
    Require(clientB.Stats()->smoothedRttMilliseconds > 0U && link.dropped > 0U, "the report has no round trip time or no loss");
}

void TestResponseFloodIsLimitedPerAddress() {
    Rig rig;
    const auto start = rig.Run(0U, 1000U, [&] { return rig.client->State() == ClientState::Connected; });
    Require(rig.client->State() == ClientState::Connected, "initial client did not connect");
    const auto packet = std::ranges::find_if(rig.clientTap->sent, [](const auto& data) { return data.size() > 3U && data[3] == 3U; });
    Require(packet != rig.clientTap->sent.end(), "no authenticated Response captured");
    const auto response = *packet;
    const auto accepts = [&] { return std::ranges::count_if(rig.hostTap->sent, [](const auto& data) { return data.size() > 3U && data[3] == 4U; }); };
    const auto acceptsBefore = accepts();
    for (std::uint64_t now = start; now < start + 1000U; ++now) {
        rig.network.Inject(rig.clientPort->LocalEndpoint(), rig.hostPort->LocalEndpoint(), response);
        rig.network.Advance(now);
        rig.host->Update(now);
    }
    Require(accepts() - acceptsBefore <= 15 && rig.host->Stats().handshakesRateLimited >= 980U,
        "an authenticated Response flood was not limited per address");
    auto other = rig.network.CreateTransport(Endpoint::Make(127, 0, 0, 2, 7002));
    rig.keys.Set(43U, MakeKey(9U));
    Client guest(*other, {}, 43U, MakeKey(9U), rig.hostPort->LocalEndpoint(), start + 1000U);
    for (std::uint64_t now = start + 1000U; now < start + 1100U; now += 10U) {
        rig.network.Advance(now); rig.host->Update(now); guest.Update(now);
    }
    Require(guest.State() == ClientState::Connected, "another proven address was blocked by the flood");
    const auto limitedBefore = rig.host->Stats().handshakesRateLimited;
    rig.network.Inject(rig.clientPort->LocalEndpoint(), rig.hostPort->LocalEndpoint(), response);
    rig.host->Update(start + 11'000U);
    Require(rig.host->Stats().handshakesRateLimited == limitedBefore, "the address bucket did not recover");
}

void TestIdleHostDoesNotAllocate() {
    LoopbackNetwork network;
    auto socket = network.CreateTransport(Endpoint::Loopback(7000U));
    StaticKeyTable keys;
    HostConfig config;
    config.maxClients = config.maxSessionsPerAddress = 512U;
    config.maxConnectsPerSecondPerAddress = 4096U;
    Host host(*socket, config, keys);
    std::vector<std::unique_ptr<IDatagramTransport>> ports;
    std::vector<std::unique_ptr<Client>> clients;
    for (std::uint64_t id = 2U; id < 302U; ++id) {
        keys.Set(id, MakeKey(9U));
        ports.push_back(network.CreateTransport(Endpoint::Loopback(static_cast<std::uint16_t>(7100U + id))));
        clients.push_back(std::make_unique<Client>(*ports.back(), ClientConfig{}, id, MakeKey(9U), socket->LocalEndpoint(), 0U));
    }
    for (std::uint64_t now = 0U; now < 200U; ++now) {
        network.Advance(now); host.Update(now);
        for (auto& client : clients) client->Update(now);
        NetEvent event;
        while (host.PollEvent(event)) {}
    }
    Require(host.ClientCount() == 300U && std::ranges::all_of(clients, [](const auto& client) { return client->State() == ClientState::Connected; }), "idle clients did not connect");
    network.Advance(200U); host.Update(200U);
    network_allocations::count = 0U;
    network_allocations::enabled = true;
    for (int iteration = 0; iteration < 1000; ++iteration) host.Update(200U);
    network_allocations::enabled = false;
    Require(network_allocations::count == 0U, "idle Host::Update allocated " + std::to_string(network_allocations::count) + " times");
    std::printf("  idle: 300 sessions, 1000 updates, zero allocations\n");
}

void TestUdpWaitReadableWakesOnDatagram() {
    std::string error;
    auto receiver = CreateUdpTransport(Endpoint::Loopback(0U), error);
    auto sender = CreateUdpTransport(Endpoint::Loopback(0U), error);
    Require(receiver && sender, error);
    const auto target = receiver->LocalEndpoint();
    const auto start = std::chrono::steady_clock::now();
    std::thread delivery([&] { std::this_thread::sleep_for(std::chrono::milliseconds(20)); static_cast<void>(sender->Send(target, std::array<std::uint8_t, 1>{1U})); });
    const bool ready = receiver->WaitReadable(100U);
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    delivery.join();
    Require(ready && elapsed >= 15.0 && elapsed <= 100.0, "UDP readiness did not wake on the datagram");
    std::array<std::uint8_t, 16> bytes{};
    Endpoint from; std::size_t size;
    Require(receiver->Receive(from, bytes, size) == TransportStatus::Ok, "readiness consumed the datagram");
    const auto idleStart = std::chrono::steady_clock::now();
    Require(!receiver->WaitReadable(30U), "idle UDP socket reported readable");
    const auto idle = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - idleStart).count();
    Require(idle >= 15.0 && idle <= 45.0, "UDP readiness timeout is outside tolerance");
}

void TestSessionDeadlineTracksPendingWork() {
    SessionPair pair;
    std::vector<std::uint8_t> packet;
    packet.reserve(kMaxDatagramBytes);
    Require(pair.a.NextDueMs(0U) == 0U && pair.a.BuildPacket(0U, packet), "initial session deadline was not immediate");
    Require(pair.a.NextDueMs(1U) == 1000U && !pair.a.BuildPacket(1U, packet), "idle session ignored the keepalive deadline");
    Require(pair.a.Send(0U, Pattern(1U, 10U)) == SendResult::Queued && pair.a.NextDueMs(10U) == 10U, "Send did not advance the deadline");
    Require(pair.a.BuildPacket(10U, packet) && pair.a.NextDueMs(11U) == 260U, "reliable retransmission deadline differs");
    Require(pair.b.ProcessPacket(10U, packet) == PacketResult::Accepted && pair.b.NextDueMs(10U) == 10U, "received reliable data did not schedule its ACK");
    Require(pair.b.BuildPacket(20U, packet) && pair.a.ProcessPacket(20U, packet) == PacketResult::Accepted, "session ACK failed");
    Require(pair.a.QueuedSendBytes() == 0U && pair.a.NextDueMs(21U) == 1010U, "ACK did not remove the retransmission deadline");
    pair.a.Poke();
    Require(pair.a.NextDueMs(21U) == 21U && pair.a.BuildPacket(21U, packet), "Poke did not advance the deadline");
    pair.a.Disconnect();
    Require(pair.a.NextDueMs(22U) == 22U, "Disconnect did not advance the deadline");
}

void TestPacketRecordLimitMatchesReceiverCapacity() {
    SessionPair pair;
    for (std::uint32_t i = 0U; i < 100U; ++i) Require(pair.a.Send(1U, Pattern(i, 4U)) == SendResult::Queued, "small record was refused");
    Require(Exchange(pair.a, pair.b, 0U) == 2U, "small records were not split at the packet record limit");
    Message message;
    std::size_t count = 0U;
    while (pair.b.PopMessage(message)) ++count;
    Require(count == 100U && pair.b.Stats().rejectedMalformed == 0U, "sender exceeded receiver record capacity");
}

void TestSpoofedConnectsDoNotBlockVictimJoin() {
    Rig rig;
    std::vector<std::uint8_t> connect{0x4EU, 0x4BU, 2U, 1U};
    ByteWriter writer(connect);
    writer.U64(42U);
    writer.U64(1U);
    writer.Zeros(kMinConnectBytes - connect.size());
    for (int i = 0; i < 1000; ++i) rig.network.Inject(rig.clientPort->LocalEndpoint(), rig.hostPort->LocalEndpoint(), connect);
    rig.host->Update(0U);
    Require(rig.host->Stats().challengesSent == 1000U && rig.host->Stats().handshakesRateLimited == 0U && rig.host->ClientCount() == 0U,
        "unproven Connects consumed the victim's rate bucket");
    const auto& challenge = rig.hostTap->sent.front();
    Require(challenge.size() < connect.size(), "a spoofed Connect caused amplification");
    std::vector<std::uint8_t> invalid{0x4EU, 0x4BU, 2U, 3U};
    ByteWriter response(invalid);
    response.U64(42U); response.U64(1U);
    response.Bytes(std::span<const std::uint8_t>(challenge).subspan(12U, 8U));
    response.Zeros(64U);
    for (int i = 0; i < 16; ++i) rig.network.Inject(rig.clientPort->LocalEndpoint(), rig.hostPort->LocalEndpoint(), invalid);
    rig.host->Update(0U);
    Require(rig.host->Stats().handshakesRejected == 16U && rig.host->Stats().handshakesRateLimited == 0U,
        "Responses with invalid MACs consumed the victim's rate bucket");
    rig.Run(0U, 100U, [&] { return rig.client->State() == ClientState::Connected; });
    Require(rig.client->State() == ClientState::Connected && rig.host->ClientCount() == 1U,
        "spoofed Connects blocked the victim's join before token refill");
}

void TestFullBucketTableDoesNotLockOutNewAddresses() {
    HostConfig config;
    config.maxClients = 1U;
    Rig rig({}, config);
    const auto now = rig.Run(0U, 1000U, [&] { return rig.client->State() == ClientState::Connected; });
    Require(rig.client->State() == ClientState::Connected, "bucket test initial client did not connect");
    rig.keys.Set(43U, MakeKey(9U));
    const auto exchange = [&](Client& guest, std::uint64_t time) {
        for (int step = 0; step < 6; ++step) {
            guest.Update(time); rig.network.Advance(time); rig.host->Update(time); rig.network.Advance(time);
        }
    };
    for (std::uint32_t index = 0; index < 8191U; ++index) {
        auto port = rig.network.CreateTransport(Endpoint{0x0A000001U + index, 7002U});
        Client guest(*port, {}, 43U, MakeKey(9U), rig.hostPort->LocalEndpoint(), now);
        exchange(guest, now);
    }
    Require(rig.host->Stats().handshakesRejected == 8191U && rig.host->Stats().connectBucketsFull == 0U,
        "authenticated Responses did not fill the bucket table exactly");
    rig.host->Disconnect(42U);
    rig.host->Update(now); rig.host->Update(now);
    Require(rig.host->ClientCount() == 0U, "bucket test could not release session capacity");
    auto port = rig.network.CreateTransport(Endpoint::Make(10, 1, 0, 1, 7002U));
    Client guest(*port, {}, 43U, MakeKey(9U), rig.hostPort->LocalEndpoint(), now);
    exchange(guest, now);
    Require(guest.State() == ClientState::Connected && rig.host->Stats().connectBucketsFull > 0U,
        "a full bucket table locked out a new proven address");
    bool named = false;
    for (const auto& entry : rig.host->Diagnostics().Entries()) named |= entry.code == DiagCode::ConnectBucketsFull && entry.count > 0U && DiagName(entry.code) == "ConnectBucketsFull";
    Require(named, "bucket exhaustion was not named and counted");
    const auto fullBefore = rig.host->Stats().connectBucketsFull;
    rig.host->Disconnect(43U);
    rig.host->Update(now); rig.host->Update(now);
    auto recoveredPort = rig.network.CreateTransport(Endpoint::Make(10, 1, 0, 2, 7002U));
    Client recovered(*recoveredPort, {}, 43U, MakeKey(9U), rig.hostPort->LocalEndpoint(), now + 125U);
    exchange(recovered, now + 125U);
    Require(recovered.State() == ClientState::Connected && rig.host->Stats().connectBucketsFull == fullBefore,
        "a full table did not immediately sweep refilled buckets before the periodic sweep");
}

void TestUnknownIdGetsIndistinguishableChallenge() {
    const auto challenge = [](std::uint64_t id) {
        Rig rig({}, {}, {}, id);
        rig.client->Update(0U);
        rig.network.Advance(1U);
        rig.host->Update(1U);
        Require(rig.hostTap->sent.size() == 1U, "no Challenge for a client id");
        return rig.hostTap->sent.front();
    };
    const auto known = challenge(42U);
    const auto unknown = challenge(99U);
    Require(known.size() == unknown.size() && known.size() < kMinConnectBytes &&
        std::equal(known.begin(), known.begin() + 4, unknown.begin()), "Challenge reveals whether an id exists");
    Require(known.size() == 52U, "Challenge has an unexpected discriminator");
}

void TestSessionsPerAddressLimit() {
    HostConfig config;
    config.maxSessionsPerAddress = 2U;
    Rig rig({}, config);
    rig.keys.Set(43U, MakeKey(9U));
    rig.keys.Set(44U, MakeKey(9U));
    auto secondPort = rig.network.CreateTransport(Endpoint::Loopback(7002));
    auto thirdPort = rig.network.CreateTransport(Endpoint::Loopback(7003));
    Client second(*secondPort, {}, 43U, MakeKey(9U), rig.hostPort->LocalEndpoint(), 0U);
    Client third(*thirdPort, {}, 44U, MakeKey(9U), rig.hostPort->LocalEndpoint(), 0U);
    for (std::uint64_t now = 0; now < 2000U; now += 10U) {
        rig.Step(now);
        second.Update(now);
        third.Update(now);
    }
    Require(rig.host->ClientCount() == 2U && rig.host->Stats().sessionsPerAddressRefused > 0U,
        "the session limit per address was not enforced");
    bool named = false;
    for (const auto& entry : rig.host->Diagnostics().Entries()) {
        named = named || (entry.code == DiagCode::HandshakeServerFull && std::string_view(entry.detail).find("per address") != std::string_view::npos);
    }
    Require(named, "the per-address refusal was not named");
}

void TestHandshakeAuthentication() {
    // An attacker who does not know the key can obtain a Challenge, but a Response with a made-up MAC
    // is refused, whatever cookie it carries.
    {
        Rig rig;
        auto attacker = rig.network.CreateTransport(Endpoint::Loopback(7500));
        std::vector<std::uint8_t> connect{ 0x4EU, 0x4BU, 2U, 1U };
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
        std::vector<std::uint8_t> response{ 0x4EU, 0x4BU, 2U, 3U };
        ByteWriter responseWriter(response);
        responseWriter.U64(42U);
        responseWriter.U64(0x1122334455667788ULL);
        responseWriter.Bytes(std::span<const std::uint8_t>(challenge.data() + 12U, 8U)); // the host's cookie
        responseWriter.Zeros(32U);
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
        Require(rig.host->Stats().challengesSent > 0U, "an unknown client id did not receive a challenge");
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

std::vector<std::uint8_t> PacketOf(const TappingTransport& tap, std::uint8_t type) {
    for (const auto& packet : tap.sent) if (packet.size() >= 4U && packet[3] == type) return packet;
    throw std::runtime_error("expected datagram type was not recorded");
}

HostConfig PasswordHost(std::string password = "sample-password") {
    HostConfig config; config.admission = Admission::JoinPassword; config.joinPassword = std::move(password);
    return config;
}
ClientConfig PasswordClient(std::string password = "sample-password") {
    ClientConfig config; config.admission = Admission::JoinPassword; config.joinPassword = std::move(password);
    return config;
}

void TestJoinPasswordAdmitsMatchingGuest() {
    Rig rig({}, PasswordHost(), PasswordClient(), kHostPlayerId);
    auto now = rig.Run(0U, 1000U, [&] { return rig.client->State() == ClientState::Connected; });
    Require(rig.client->State() == ClientState::Connected && rig.client->GuestAuthenticated(), "matching password did not authenticate");
    const auto id = rig.client->AssignedId();
    Require(id > kHostPlayerId && rig.host->ClientIds() == std::vector<std::uint64_t>{id}, "host did not assign the identity");
    Require(rig.client->Send(0U, Pattern(15U, 20U)) == SendResult::Queued && rig.host->Send(id, 0U, Pattern(16U, 20U)) == SendResult::Queued, "password session send failed");
    rig.Run(now + 10U, now + 1000U, [&] { return Rig::HasEvent(rig.hostEvents, NetEventType::Message) && Rig::HasEvent(rig.clientEvents, NetEventType::Message); });
    Require(Rig::HasEvent(rig.hostEvents, NetEventType::Message) && Rig::HasEvent(rig.clientEvents, NetEventType::Message), "PAKE and ephemeral DH did not agree on session keys");
    Require(rig.host->Stats().passwordAttempts == 1U, "matching exchange counted more than once");
}

void TestWrongPasswordNeverConnects() {
    Rig rig({}, PasswordHost(), PasswordClient("wrong-password"));
    std::uint64_t now = 0U;
    for (; now < 1000U; now += 10U) {
        rig.Step(now);
        if (std::ranges::any_of(rig.clientTap->sent, [](const auto& p) { return p[3] == 7U; })) break;
    }
    Require(now < 1000U && rig.host->Stats().passwordRejected == 0U && rig.host->ClientCount() == 0U &&
        rig.client->State() == ClientState::Connecting, "wrong password was checked before online confirmation");
    rig.Run(now + 10U, now + 500U, [&] { return rig.client->State() == ClientState::Disconnected; });
    Require(rig.client->State() == ClientState::Disconnected && rig.host->Stats().passwordRejected == 1U &&
        rig.host->Stats().handshakesRejected == 1U && rig.host->ClientCount() == 0U &&
        rig.client->RejectInfo() != nullptr && rig.client->RejectInfo()->reason == RejectReason::PasswordRejected, "wrong password was not refused online");
    Require(std::ranges::none_of(rig.hostTap->sent, [](const auto& p) { return p[3] == 4U; }), "wrong password received Accept");
    Require(std::ranges::any_of(rig.host->Diagnostics().Entries(), [](const auto& e) { return e.code == DiagCode::PasswordRejected && e.count == 1U; }), "password refusal has no named counter");
}

void TestPasswordHandshakeSurvivesPacketLoss() {
    auto client = PasswordClient(); client.connectTimeoutMilliseconds = 30'000U;
    Rig rig(BadNetwork(), PasswordHost(), client);
    rig.Run(0U, 30'000U, [&] { return rig.client->State() == ClientState::Connected; });
    Require(rig.client->State() == ClientState::Connected && rig.host->ClientCount() == 1U &&
        rig.host->Stats().passwordAttempts == 1U && rig.host->Stats().handshakesCompleted == 1U,
        "PAKE retransmissions lost the exchange or created another session");
}

void TestPasswordConfirmationBindsEphemeralKey() {
    Rig rig({}, PasswordHost(), PasswordClient());
    std::uint64_t now = 0U;
    for (; now < 1000U; now += 10U) {
        rig.network.Advance(now); rig.host->Update(now);
        if (std::ranges::any_of(rig.hostTap->sent, [](const auto& p) { return p[3] == 6U; })) break;
        rig.client->Update(now);
    }
    Require(now < 1000U, "password exchange did not reach server public message");
    rig.network.Advance(now + 1U);
    std::array<std::uint8_t, kMaxDatagramBytes> bytes{};
    Endpoint from; std::size_t size = 0U;
    while (rig.clientPort->Receive(from, bytes, size) == TransportStatus::Ok) {}
    auto challenge = PacketOf(*rig.hostTap, 6U);
    challenge[52U] ^= 1U;
    rig.network.Inject(rig.hostPort->LocalEndpoint(), rig.clientPort->LocalEndpoint(), challenge);
    rig.Run(now + 10U, now + 1000U, [&] { return rig.client->State() == ClientState::Disconnected; });
    Require(rig.host->Stats().passwordRejected == 1U && rig.host->ClientCount() == 0U &&
        rig.client->State() == ClientState::Disconnected, "changed ephemeral key was not bound to PAKE confirmation");
}

void TestPasswordAttemptsAreLimitedAfterCookie() {
    Rig rig({}, PasswordHost(), PasswordClient("wrong-password"));
    std::uint64_t now = 0U;
    for (std::uint64_t i = 0U; i < 10U; ++i) {
        auto config = PasswordClient("wrong-password"); config.clientSalt = 100U + i;
        rig.client = std::make_unique<Client>(*rig.clientTap, config, 42U, SessionKey{}, rig.hostPort->LocalEndpoint(), now);
        for (unsigned step = 0U; step < 8U; ++step) rig.Step(now++);
    }
    Require(rig.host->Stats().passwordAttempts == 8U && rig.host->Stats().passwordRejected == 8U && rig.host->Stats().passwordRateLimited > 0U,
        "password rate limit did not bound online guesses");
    Require(std::ranges::any_of(rig.host->Diagnostics().Entries(), [](const auto& e) { return e.code == DiagCode::PasswordRateLimited; }), "password rate limit was unnamed");
    const auto attempts = rig.host->Stats().passwordAttempts;
    auto response = PacketOf(*rig.clientTap, 3U); response[20] ^= 128U;
    rig.network.Inject(rig.clientPort->LocalEndpoint(), rig.hostPort->LocalEndpoint(), response);
    rig.Step(now);
    Require(rig.host->Stats().passwordAttempts == attempts, "invalid cookie consumed a password attempt");
}

void TestOpenAdmissionAssignsUniqueIds() {
    LoopbackNetwork network;
    auto hostPort = network.CreateTransport(Endpoint::Loopback(7000));
    auto config = PasswordHost(""); config.maxClients = 256U; config.maxConnectsPerSecondPerAddress = 512U; config.maxSessionsPerAddress = 256U;
    Host host(*hostPort, config);
    std::size_t logLines = 0U;
    host.Diagnostics().SetSink([&](std::string_view line) { if (line.starts_with("UnauthenticatedGuests:")) ++logLines; });
    std::vector<std::unique_ptr<IDatagramTransport>> ports;
    std::vector<std::unique_ptr<Client>> clients;
    for (unsigned i = 0U; i < 200U; ++i) {
        ports.push_back(network.CreateTransport(Endpoint::Loopback(static_cast<std::uint16_t>(7100U + i))));
        clients.push_back(std::make_unique<Client>(*ports.back(), PasswordClient(""), hostPort->LocalEndpoint(), 0U));
    }
    for (std::uint64_t now = 0U; now < 1000U; now += 10U) { network.Advance(now); host.Update(now); for (auto& client : clients) client->Update(now); }
    auto ids = host.ClientIds(); std::ranges::sort(ids);
    Require(ids.size() == 200U && std::adjacent_find(ids.begin(), ids.end()) == ids.end() && ids.front() > kHostPlayerId,
        "open guests did not receive unique host identities");
    for (const auto& client : clients) Require(client->State() == ClientState::Connected && !client->GuestAuthenticated() && std::ranges::binary_search(ids, client->AssignedId()), "open Accept did not declare unauthenticated guest identity");
    const auto victim = clients.front()->AssignedId();
    auto attackerPort = network.CreateTransport(Endpoint::Loopback(7400));
    Client attacker(*attackerPort, PasswordClient(""), victim, SessionKey{}, hostPort->LocalEndpoint(), 1000U);
    for (std::uint64_t now = 1000U; now < 1400U; now += 10U) { network.Advance(now); host.Update(now); attacker.Update(now); for (auto& client : clients) client->Update(now); }
    Require(attacker.State() == ClientState::Connected && attacker.AssignedId() != victim && host.ClientCount() == 201U &&
        clients.front()->State() == ClientState::Connected && host.ClientStats(victim) != nullptr, "claimed guest identity replaced an existing guest");
    NetEvent event; while (host.PollEvent(event)) Require(event.type != NetEventType::Disconnected, "open admission emitted Replaced or disconnected a guest");
    Require(logLines > 0U, "unauthenticated admission was not logged");
}

void TestPassivelyRecordedHandshakeDoesNotVerifyPasswordGuesses() {
    Rig rig({}, PasswordHost("word-537"), PasswordClient("word-537"));
    rig.Run(0U, 1000U, [&] { return rig.client->State() == ClientState::Connected; });
    Require(rig.client->State() == ClientState::Connected, "recording fixture failed");
    const auto challenge = PacketOf(*rig.hostTap, 2U), pake = PacketOf(*rig.hostTap, 6U), finish = PacketOf(*rig.clientTap, 7U);
    ByteReader salt(std::span(challenge).subspan(4U)); const auto clientSalt = salt.U64();
    // Regression for a deterministic password verifier, not a statistical proof of the DH assumption.
    // All dictionary entries, including the correct one, generate the same message shapes with fresh blinded points.
    for (unsigned guess = 0U; guess < 1000U; ++guess) {
        LoopbackNetwork offline;
        auto port = offline.CreateTransport(Endpoint::Loopback(7001)); TappingTransport tap(*port);
        auto config = PasswordClient("word-" + std::to_string(guess)); config.clientSalt = clientSalt;
        Client candidate(tap, config, 42U, SessionKey{}, Endpoint::Loopback(7000), 0U);
        offline.Inject(Endpoint::Loopback(7000), port->LocalEndpoint(), challenge); offline.Advance(0U); candidate.Update(0U);
        offline.Inject(Endpoint::Loopback(7000), port->LocalEndpoint(), pake); offline.Advance(1U); candidate.Update(1U);
        const auto response = PacketOf(tap, 3U), confirmation = PacketOf(tap, 7U);
        Require(response.size() == 92U && confirmation.size() == finish.size() && candidate.State() == ClientState::Connecting &&
            !kb::security::ConstantTimeEqual(confirmation, finish), "recorded confirmation became a deterministic password verifier");
    }
    Require(rig.host->Stats().passwordAttempts == 1U && rig.host->Stats().passwordRejected == 0U, "dictionary evaluation made online queries");
}

void TestPasswordHandshakeReplayDoesNotCreateSession() {
    auto hostConfig = PasswordHost(); hostConfig.maxConnectsPerSecondPerAddress = 64U;
    Rig rig({}, hostConfig, PasswordClient());
    auto now = rig.Run(0U, 1000U, [&] { return rig.client->State() == ClientState::Connected; });
    const auto response = PacketOf(*rig.clientTap, 3U), finish = PacketOf(*rig.clientTap, 7U), accept = PacketOf(*rig.hostTap, 4U);
    const auto assigned = rig.client->AssignedId();
    const auto completed = rig.host->Stats().handshakesCompleted;
    for (unsigned i = 0U; i < 3U; ++i) {
        rig.network.Inject(rig.clientPort->LocalEndpoint(), rig.hostPort->LocalEndpoint(), response);
        rig.network.Inject(rig.clientPort->LocalEndpoint(), rig.hostPort->LocalEndpoint(), finish); rig.Step(now += 10U);
    }
    Require(rig.host->Stats().handshakesCompleted == completed && rig.host->ClientCount() == 1U &&
        rig.client->AssignedId() == assigned && PacketOf(*rig.hostTap, 4U) == accept, "replay reset an established session");
    rig.host->Disconnect(assigned);
    rig.Run(now + 10U, now + 1000U, [&] { return rig.host->ClientCount() == 0U; });
    rig.network.Inject(rig.clientPort->LocalEndpoint(), rig.hostPort->LocalEndpoint(), response); rig.Step(now += 1010U);
    rig.network.Inject(rig.clientPort->LocalEndpoint(), rig.hostPort->LocalEndpoint(), finish); rig.Step(now += 10U);
    Require(rig.host->ClientCount() == 0U && rig.host->Stats().handshakesCompleted == completed && rig.host->Stats().passwordRejected > 0U,
        "recorded PAKE confirmation created a new session");
}

std::array<std::uint8_t, 64U> OpenAcceptKeys(const std::vector<std::uint8_t>& response, const std::vector<std::uint8_t>& accept) {
    Require(accept.size() == 120U, "Accept has the wrong size");
    const auto label = [](std::string_view text) {
        return std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
    };
    const std::span<const std::uint8_t> salts(response.data() + 12U, 16U);
    kb::security::AeadKey key;
    kb::security::HkdfSha512(key.Span(), MakeKey(9U), salts, label("21kb-net-accept-v2"));
    kb::security::AeadNonce nonce{};
    std::copy_n(accept.begin() + 4, nonce.size(), nonce.begin());
    kb::security::AeadTag tag{};
    std::copy_n(accept.end() - 16, tag.size(), tag.begin());
    std::vector<std::uint8_t> plaintext(accept.begin() + 28, accept.end() - 16);
    Require(kb::security::AeadDecryptInPlace(plaintext, tag, key, nonce, std::span<const std::uint8_t>(accept).first(28U)),
        "Accept did not authenticate");
    std::vector<std::uint8_t> salt(salts.begin(), salts.end());
    salt.insert(salt.end(), plaintext.begin(), plaintext.begin() + 32);
    std::array<std::uint8_t, 64U> keys{};
    kb::security::HkdfSha512(keys, MakeKey(9U), salt, label("21kb-net-session-v2"));
    kb::security::SecureWipe(plaintext);
    return keys;
}

void TestRecordedSessionSurvivesKeyLeak() {
    Rig rig;
    const auto now = rig.Run(0U, 2000U, [&] { return rig.client->State() == ClientState::Connected; });
    Require(rig.client->State() == ClientState::Connected, "connection failed");
    rig.Run(now, now + 100U, [] { return false; });
    const auto response = PacketOf(*rig.clientTap, 3U);
    const auto accept = PacketOf(*rig.hostTap, 4U);
    const auto leakedKeys = OpenAcceptKeys(response, accept);
    SessionKey send{}, receive{};
    std::copy_n(leakedKeys.begin(), 32U, send.begin());
    std::copy_n(leakedKeys.begin() + 32, 32U, receive.begin());
    Session attacker({}, send, receive, 0U);
    Require(attacker.ProcessPacket(now + 100U, PacketOf(*rig.hostTap, 0x10U)) == PacketResult::AuthFailed,
        "a leaked PSK decrypted a recorded session without the ephemeral secret");
}

void TestEphemeralKeysDifferPerHandshake() {
    ClientConfig config;
    config.clientSalt = 123U;
    Rig first({}, {}, config), second({}, {}, config);
    first.client->Update(0U);
    second.client->Update(0U);
    const auto a = PacketOf(*first.clientTap, 1U), b = PacketOf(*second.clientTap, 1U);
    Require(a.size() == 128U && b.size() == 128U && !std::equal(a.begin() + 32, a.begin() + 64, b.begin() + 32),
        "client ephemeral key repeats across handshakes");
    first.Run(0U, 2000U, [&] { return first.client->State() == ClientState::Connected; });
    const auto response = PacketOf(*first.clientTap, 3U);
    Require(response.size() == 92U, "Response does not carry the ephemeral key");
    auto tampered = response;
    tampered[28] ^= 1U;
    const auto before = first.host->Stats().handshakesRejected;
    first.network.Inject(Endpoint::Loopback(7001), Endpoint::Loopback(7000), tampered);
    first.network.Advance(2001U);
    first.host->Update(2001U);
    Require(first.host->Stats().handshakesRejected == before + 1U, "Response MAC did not authenticate the ephemeral key");
}

void TestReplayedResponseGetsFreshKeys() {
    Rig rig;
    const std::uint64_t connected = rig.Run(0U, 2000U, [&] { return rig.client->State() == ClientState::Connected; });
    Require(rig.client->State() == ClientState::Connected, "initial connection failed");
    rig.Run(connected, connected + 100U, [] { return false; });
    const auto response = PacketOf(*rig.clientTap, 3U);
    const auto oldAccept = PacketOf(*rig.hostTap, 4U);
    const auto oldClientData = PacketOf(*rig.clientTap, 0x10U);
    const auto oldServerData = PacketOf(*rig.hostTap, 0x10U);
    const auto oldKeys = OpenAcceptKeys(response, oldAccept);
    rig.host->Disconnect(42U);
    rig.host->Update(connected + 110U);
    rig.host->Update(connected + 120U);
    Require(rig.host->ClientCount() == 0U, "the first session was not removed");
    rig.hostTap->sent.clear();
    rig.network.Inject(Endpoint::Loopback(7001), Endpoint::Loopback(7000), response);
    rig.network.Advance(connected + 130U);
    rig.host->Update(connected + 130U);
    Require(rig.host->ClientCount() == 1U && rig.host->Stats().handshakesCompleted == 2U, "valid replay did not reach a fresh session");
    const auto freshAccept = PacketOf(*rig.hostTap, 4U);
    const auto freshServerData = PacketOf(*rig.hostTap, 0x10U);
    const auto freshKeys = OpenAcceptKeys(response, freshAccept);
    Require(oldKeys != freshKeys && oldAccept != freshAccept && oldServerData != freshServerData,
        "replayed Response reused session keys or server ciphertext");
    const auto authBefore = rig.client->Stats()->rejectedAuth;
    for (std::uint64_t index = 0U; index <= rig.client->Stats()->packetsAccepted; ++index) {
        Require(rig.host->Send(42U, 1U, Pattern(99U, 16U)) == SendResult::Queued, "fresh session could not send");
        rig.host->Update(connected + 132U);
    }
    const auto unusedServerData = rig.hostTap->sent.back();
    rig.network.Advance(connected + 133U);
    Endpoint discardedFrom;
    std::array<std::uint8_t, kMaxDatagramBytes> discarded{};
    std::size_t discardedSize = 0U;
    while (rig.clientPort->Receive(discardedFrom, discarded, discardedSize) == TransportStatus::Ok) { }
    rig.network.Inject(Endpoint::Loopback(7000), Endpoint::Loopback(7001), unusedServerData);
    rig.network.Advance(connected + 135U);
    rig.client->Update(connected + 135U);
    Require(rig.client->Stats()->rejectedAuth == authBefore + 1U, "fresh server data authenticated under old keys");
    rig.network.Inject(Endpoint::Loopback(7001), Endpoint::Loopback(7000), oldClientData);
    rig.network.Advance(connected + 140U);
    rig.host->Update(connected + 140U);
    Require(rig.host->ClientStats(42U)->rejectedAuth == 1U && rig.host->ClientStats(42U)->packetsAccepted == 0U,
        "old client packet was not rejected as AuthFailed");
    const auto packetsSent = rig.host->ClientStats(42U)->packetsSent;
    rig.hostTap->sent.clear();
    rig.network.Inject(Endpoint::Loopback(7001), Endpoint::Loopback(7000), response);
    rig.network.Advance(connected + 150U);
    rig.host->Update(connected + 150U);
    Require(PacketOf(*rig.hostTap, 4U) == freshAccept && rig.host->Stats().handshakesCompleted == 2U &&
        rig.host->ClientStats(42U)->packetsSent == packetsSent, "duplicate Response reset the live session");
}

void TestVersionMismatchIsRefusedWithReason() {
    Require(MakeBuildIdentity(5U, "abc").contentHash == 0xBA7A6193A135AFDDULL &&
        MakeBuildIdentity(5U, "").contentHash == 0U, "content hash encoding is incorrect");
    {
        Rig unchecked;
        unchecked.Run(0U, 1000U, [] { return false; });
        const auto hostEntries = unchecked.host->Diagnostics().Entries();
        const auto clientEntries = unchecked.client->Diagnostics().Entries();
        Require(hostEntries.size() == 1U && clientEntries.size() == 1U &&
            hostEntries[0].code == DiagCode::VersionUnchecked && hostEntries[0].count == 1U &&
            clientEntries[0].code == DiagCode::VersionUnchecked && clientEntries[0].count == 1U,
            "unchecked build identity was not reported once at startup");
    }
    HostConfig host;
    host.identity = MakeBuildIdentity(3U, "content-a");
    host.minClientRelease = 3U;
    ClientConfig client;
    client.identity = MakeBuildIdentity(2U, "content-a");
    Rig rig({}, host, client);
    const auto now = rig.Run(0U, 100U, [&] { return rig.client->State() == ClientState::Disconnected; });
    Require(now <= 20U && rig.client->State() == ClientState::Disconnected &&
        rig.clientEvents.back().reason == DisconnectReason::VersionMismatch, "outdated release timed out or lost its reason");
    const auto* info = rig.client->RejectInfo();
    Require(info != nullptr && info->reason == RejectReason::OutdatedClient && info->server.release == 3U &&
        info->server.contentHash == host.identity.contentHash, "server identity was lost from Reject");
    Require(rig.host->Stats().challengesSent == 0U && PacketOf(*rig.hostTap, 5U).size() == 25U, "version refusal was late or amplified");
    client.identity = MakeBuildIdentity(3U, "content-b");
    Rig mismatch({}, host, client);
    mismatch.Run(0U, 100U, [&] { return mismatch.client->State() == ClientState::Disconnected; });
    Require(mismatch.client->State() == ClientState::Disconnected && mismatch.clientEvents.back().reason == DisconnectReason::ContentMismatch &&
        mismatch.client->RejectInfo()->reason == RejectReason::ContentMismatch, "content mismatch was not named");
}

void TestUnconfirmedPeerExpires() {
    HostConfig config;
    config.confirmMilliseconds = 500U;
    Rig rig({}, config);
    rig.client->Update(0U);
    rig.network.Advance(10U);
    rig.host->Update(10U);
    rig.network.Advance(20U);
    rig.client->Update(20U);
    Require(rig.client->Stats() == nullptr && rig.client->State() == ClientState::Connecting, "Challenge created a client session");
    rig.network.Advance(30U);
    rig.host->Update(30U);
    Require(rig.host->ClientCount() == 1U, "Response did not create the pending peer");
    const auto response = PacketOf(*rig.clientTap, 3U);
    const auto accept = PacketOf(*rig.hostTap, 4U);
    for (std::uint64_t now = 100U; now <= 500U; now += 100U) {
        rig.network.Inject(Endpoint::Loopback(7001), Endpoint::Loopback(7000), response);
        rig.network.Advance(now);
        rig.host->Update(now);
        Require(rig.host->ClientCount() == 1U && rig.hostTap->sent.back() == accept, "pending Response changed Accept");
    }
    rig.host->Update(530U);
    Require(rig.host->ClientCount() == 0U, "repeated Responses extended confirmation lifetime");
    Rig confirmed({}, config);
    confirmed.Run(0U, 1500U, [] { return false; });
    Require(confirmed.host->ClientCount() == 1U && confirmed.client->State() == ClientState::Connected,
        "authenticated client data did not confirm the peer");
}

void TestOldProtocolGetsReject() {
    Rig rig;
    rig.client->Update(0U);
    auto connect = PacketOf(*rig.clientTap, 1U);
    connect[2] = 1U;
    rig.hostTap->sent.clear();
    rig.network.Inject(Endpoint::Loopback(7777), Endpoint::Loopback(7000), connect);
    rig.network.Advance(10U);
    rig.host->Update(10U);
    const auto reject = PacketOf(*rig.hostTap, 5U);
    ByteReader reader(reject);
    Require(reader.U16() == 0x4B4EU && reader.U8() == 2U && reader.U8() == 5U &&
        reader.U8() == static_cast<std::uint8_t>(RejectReason::ProtocolVersion), "old protocol did not receive a named v2 Reject");
    ByteReader original(std::span<const std::uint8_t>(connect).subspan(12U));
    Require(reader.U64() == original.U64() && reject.size() <= 40U && reject.size() < connect.size(), "Reject salt or amplification bound is wrong");
    const auto replies = rig.hostTap->sent.size();
    connect.resize(20U);
    rig.network.Inject(Endpoint::Loopback(7777), Endpoint::Loopback(7000), connect);
    rig.network.Advance(20U);
    rig.host->Update(20U);
    Require(rig.hostTap->sent.size() == replies, "short old Connect received a reply");
}

void TestAcceptAuthenticationAndRetransmission() {
    Rig rig;
    rig.client->Update(0U);
    rig.network.Advance(10U); rig.host->Update(10U);
    rig.network.Advance(20U); rig.client->Update(20U);
    rig.network.Advance(30U); rig.host->Update(30U);
    const auto accept = PacketOf(*rig.hostTap, 4U);
    Endpoint from;
    std::array<std::uint8_t, kMaxDatagramBytes> buffer{};
    std::size_t size = 0U;
    rig.network.Advance(40U);
    while (rig.clientPort->Receive(from, buffer, size) == TransportStatus::Ok) { }
    for (const std::size_t offset : {4U, 28U, 87U}) {
        auto damaged = accept;
        damaged[offset] ^= 1U;
        rig.network.Inject(Endpoint::Loopback(7000), Endpoint::Loopback(7001), damaged);
    }
    rig.network.Advance(50U); rig.client->Update(50U);
    Require(rig.client->State() == ClientState::Connecting && rig.client->Stats() == nullptr, "tampered Accept created a session");
    auto reject = std::vector<std::uint8_t>{0x4EU, 0x4BU, 2U, 5U};
    ByteWriter writer(reject);
    writer.U8(4U); writer.U64(1U); writer.U32(0U); writer.U64(0U);
    rig.network.Inject(Endpoint::Loopback(7000), Endpoint::Loopback(7001), reject);
    rig.network.Advance(60U); rig.client->Update(60U);
    Require(rig.client->State() == ClientState::Connecting, "Reject with another salt was accepted");
    rig.Run(70U, 1000U, [&] { return rig.client->State() == ClientState::Connected; });
    Require(rig.client->State() == ClientState::Connected && rig.host->Stats().handshakesCompleted == 1U,
        "lost Accept did not recover by Response retransmission");
    for (const auto& packet : rig.hostTap->sent) if (packet[3] == 4U) Require(packet == accept, "Accept retransmission changed bytes");
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
            bytes[2] = 2U;
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
        std::vector<std::uint8_t> packet;
        if (pair.a.BuildPacket(static_cast<std::uint64_t>(index) * 300U, packet)) {
            genuine.push_back(packet);
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
    Require(other.RejectInfo() != nullptr && other.RejectInfo()->reason == RejectReason::ServerFull, "capacity rejection lost its reason");
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

void TestClockConvergesWithin2Seconds() {
    NetworkClock clock;
    clock.SetTickRate(30U);
    std::mt19937 rng{ 7U };
    std::uniform_real_distribution<double> jitter{ 0.0, 0.040 };
    std::uniform_real_distribution<double> loss{ 0.0, 1.0 };
    for (std::uint32_t tick = 0U; tick <= 120U; ++tick) {
        if (loss(rng) < 0.05) continue;
        const double arrival = static_cast<double>(tick) / 30.0 + 0.050 + jitter(rng);
        clock.OnState(tick, arrival);
        if (tick >= 60U) {
            Require(std::abs(clock.LocalTimeOf(tick) - (static_cast<double>(tick) / 30.0 + 0.050)) <= 0.010, "clock offset error above 10 ms after 2 s");
        }
    }
}

void TestInterpolatedReplicaIsSmoothUnderJitter() {
    NetworkClock clock;
    clock.SetTickRate(30U);
    std::mt19937 rng{ 7U };
    std::uniform_real_distribution<double> jitter{ 0.0, 0.040 };
    std::uniform_real_distribution<double> loss{ 0.0, 1.0 };
    const double v = 5.0;
    SnapshotInterpolator<double> interp{ [](const double& a, const double& b, double t) { return a + (b - a) * t; }, 0.1 };
    struct Received {
        double arrival;
        std::uint32_t tick;
    };
    std::vector<Received> received;
    for (std::uint32_t tick = 0U; tick <= 120U; ++tick) {
        if (loss(rng) < 0.05) continue;
        const double arrival = static_cast<double>(tick) / 30.0 + 0.050 + jitter(rng);
        received.push_back(Received{ arrival, tick });
    }
    std::size_t next = 0U;
    bool havePrevious = false;
    double previous = 0.0;
    const double step = 1.0 / 144.0;
    for (double local = 1.0; local <= 3.5; local += step) {
        while (next < received.size() && received[next].arrival <= local) {
            const std::uint32_t tick = received[next].tick;
            clock.OnState(tick, received[next].arrival);
            static_cast<void>(interp.Add(clock.LocalTimeOf(tick), v * static_cast<double>(tick) / 30.0));
            ++next;
        }
        interp.SetDelay(clock.ViewDelaySeconds());
        const std::optional<double> x = interp.Sample(local);
        Require(x.has_value(), "the interpolator had no snapshot");
        if (havePrevious) {
            Require(std::abs(*x - previous) <= 1.5 * v / 144.0 + 1e-9, "frame-to-frame jump too large");
        }
        Require(std::abs(*x - v * (local - clock.ViewDelaySeconds() - 0.050)) <= v / 30.0 + 0.001, "replica strays from its true path");
        previous = *x;
        havePrevious = true;
    }
}

} // namespace

int main() {
    const std::vector<std::pair<const char*, void (*)()>> tests{
        { "every-refusal-named", &TestEveryRefusalIsNamedAndCounted },
        { "diagnostics-rate-limit", &TestDiagLogIsRateLimited },
        { "byte-io", &TestByteIo },
        { "network-clock-converges", &TestClockConvergesWithin2Seconds },
        { "network-interpolation-smooth", &TestInterpolatedReplicaIsSmoothUnderJitter },
        { "idle-host-no-allocations", &TestIdleHostDoesNotAllocate },
        { "udp-readiness", &TestUdpWaitReadableWakesOnDatagram },
        { "session-deadlines", &TestSessionDeadlineTracksPendingWork },
        { "packet-record-capacity", &TestPacketRecordLimitMatchesReceiverCapacity },
        { "config-validation", &TestConfigValidation },
        { "loopback-network", &TestLoopbackNetwork },
        { "basic-exchange", &TestBasicExchange },
        { "fragmentation", &TestFragmentation },
        { "replay-window", &TestReplayAndWindow },
        { "tampered-packets", &TestTamperedPackets },
        { "backpressure", &TestBackpressureLosesNothing },
        { "queue-limits", &TestQueueLimits },
        { "response-flood-per-address", &TestResponseFloodIsLimitedPerAddress },
        { "spoofed-connect-victim-join", &TestSpoofedConnectsDoNotBlockVictimJoin },
        { "full-bucket-table-new-address", &TestFullBucketTableDoesNotLockOutNewAddresses },
        { "unknown-id-challenge", &TestUnknownIdGetsIndistinguishableChallenge },
        { "sessions-per-address", &TestSessionsPerAddressLimit },
        { "handshake-authentication", &TestHandshakeAuthentication },
        { "recorded-session-key-leak", &TestRecordedSessionSurvivesKeyLeak },
        { "join-password-matching", &TestJoinPasswordAdmitsMatchingGuest },
        { "join-password-wrong-online", &TestWrongPasswordNeverConnects },
        { "join-password-packet-loss", &TestPasswordHandshakeSurvivesPacketLoss },
        { "join-password-ephemeral-binding", &TestPasswordConfirmationBindsEphemeralKey },
        { "join-password-rate-limit", &TestPasswordAttemptsAreLimitedAfterCookie },
        { "open-assigned-identities", &TestOpenAdmissionAssignsUniqueIds },
        { "password-passive-dictionary", &TestPassivelyRecordedHandshakeDoesNotVerifyPasswordGuesses },
        { "password-handshake-replay", &TestPasswordHandshakeReplayDoesNotCreateSession },
        { "ephemeral-keys-per-handshake", &TestEphemeralKeysDifferPerHandshake },
        { "replayed-response-fresh-keys", &TestReplayedResponseGetsFreshKeys },
        { "version-refusal-reason", &TestVersionMismatchIsRefusedWithReason },
        { "unconfirmed-peer-expires", &TestUnconfirmedPeerExpires },
        { "old-protocol-reject", &TestOldProtocolGetsReject },
        { "accept-authentication-retransmission", &TestAcceptAuthenticationAndRetransmission },
        { "limits-and-flood", &TestLimitsAndFlood },
        { "disconnect-and-timeout", &TestDisconnectAndTimeout },
        { "host-capacity", &TestHostCapacity },
        { "hostile-input-session", &TestHostileInputIntoSession },
        { "hostile-input-host", &TestHostileInput },
        { "bad-network", &TestBadNetwork },
        { "two-clients-reconnect", &TestTwoClientsAndReconnectOnBadNetwork },
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
