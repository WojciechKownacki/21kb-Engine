// Tests for the WebRTC transport of kb_net: two peers in one process connect over localhost host
// candidates with in-memory signaling, exchange reliable and unreliable messages, detect a lost peer,
// and run a session handshake and message exchange through the datagram transport.

#include "kb/net/Host.hpp"
#include "kb/net/WebRtcTransport.hpp"
#include "kb/net/WebSocketTransport.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <functional>
#include <stdexcept>
#include <thread>

namespace {

using namespace kb::net;
using Clock = std::chrono::steady_clock;

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::uint64_t Millis() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());
}

WebRtcConfig LocalConfig() {
    WebRtcConfig config;
    config.bindAddress = "127.0.0.1";
    config.timeoutMilliseconds = 3000U;
    return config;
}

// Polls `step` until `done` or `limitMilliseconds` pass.
bool Pump(std::uint64_t limitMilliseconds, const std::function<void()>& step, const std::function<bool()>& done) {
    const std::uint64_t end = Millis() + limitMilliseconds;
    while (Millis() < end) {
        step();
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    step();
    return done();
}

struct LinkPair {
    std::unique_ptr<IWebRtcLink> offerer;
    std::unique_ptr<IWebRtcLink> answerer;

    void Update() const {
        if (offerer != nullptr) offerer->Update();
        if (answerer != nullptr) answerer->Update();
    }
};

LinkPair Connect(const WebRtcConfig& config) {
    auto [first, second] = CreateSignalingPair();
    LinkPair pair;
    std::string error;
    pair.offerer = CreateWebRtcOfferer(std::move(first), config, error);
    Require(pair.offerer != nullptr, error);
    pair.answerer = CreateWebRtcAnswerer(std::move(second), config, error);
    Require(pair.answerer != nullptr, error);
    Require(Pump(15'000U, [&] { pair.Update(); },
                [&] { return pair.offerer->State() == WebRtcState::Open && pair.answerer->State() == WebRtcState::Open; }),
        "the peers did not connect over localhost");
    return pair;
}

std::vector<std::uint8_t> Payload(std::uint32_t index, std::size_t size) {
    std::vector<std::uint8_t> bytes(size);
    for (std::size_t i = 0U; i < size; ++i) bytes[i] = static_cast<std::uint8_t>((index * 31U + i * 7U) & 0xFFU);
    if (size >= 4U) {
        for (std::size_t i = 0U; i < 4U; ++i) bytes[i] = static_cast<std::uint8_t>(index >> (8U * i));
    }
    return bytes;
}

std::uint32_t IndexOf(const std::vector<std::uint8_t>& bytes) {
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8U) |
        (static_cast<std::uint32_t>(bytes[2]) << 16U) | (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

std::size_t SizeOf(std::uint32_t index) { return 4U + (index * 13U) % 900U; }

// Sends `count` messages of varying size from `from` to `to` and checks what arrives.
void Exchange(const LinkPair& pair, IWebRtcLink& from, IWebRtcLink& to, WebRtcChannel channel, std::uint32_t count, bool mustArriveAll) {
    std::uint32_t sent = 0U;
    std::uint32_t received = 0U;
    std::uint32_t last = 0U;
    const auto step = [&] {
        pair.Update();
        while (sent < count) {
            const TransportStatus status = from.Send(channel, Payload(sent, SizeOf(sent)));
            if (status == TransportStatus::WouldBlock) break;
            Require(status == TransportStatus::Ok, "a message was refused");
            ++sent;
        }
        std::vector<std::uint8_t> message;
        while (to.Receive(channel, message)) {
            const std::uint32_t index = IndexOf(message);
            Require(index < count && message == Payload(index, SizeOf(index)), "a message arrived damaged");
            Require(mustArriveAll ? index == received : (received == 0U || index > last || channel == WebRtcChannel::Unreliable),
                "a reliable message arrived out of order");
            last = index;
            ++received;
            Require(received <= sent, "more messages arrived than were sent");
        }
    };
    const bool all = Pump(15'000U, step, [&] { return received == count; });
    if (mustArriveAll) Require(all, "reliable messages were lost: " + std::to_string(received) + " of " + std::to_string(count));
    Require(sent == count && received > 0U && received <= sent, "no unreliable message arrived");
}

void TestMessages() {
    LinkPair pair = Connect(LocalConfig());
    Exchange(pair, *pair.offerer, *pair.answerer, WebRtcChannel::Reliable, 1000U, true);
    Exchange(pair, *pair.answerer, *pair.offerer, WebRtcChannel::Reliable, 1000U, true);
    Exchange(pair, *pair.offerer, *pair.answerer, WebRtcChannel::Unreliable, 1000U, false);
}

void TestLargeMessage() {
    LinkPair pair = Connect(LocalConfig());
    constexpr std::size_t kSize = 256U * 1024U;
    for (const bool forward : { true, false }) {
        IWebRtcLink& from = forward ? *pair.offerer : *pair.answerer;
        IWebRtcLink& to = forward ? *pair.answerer : *pair.offerer;
        const auto message = Payload(77U, kSize);
        Require(from.Send(WebRtcChannel::Reliable, message) == TransportStatus::Ok, "the large message was refused");
        Require(from.Send(WebRtcChannel::Reliable, Payload(1U, kSize + 1U)) == TransportStatus::TooLarge, "an oversized message was accepted");
        std::vector<std::uint8_t> received;
        Require(Pump(15'000U, [&] { pair.Update(); }, [&] { return to.Receive(WebRtcChannel::Reliable, received); }), "the large message did not arrive");
        Require(received == message, "the large message arrived damaged");
    }
}

void TestDisconnect() {
    const WebRtcConfig config = LocalConfig();
    {
        LinkPair pair = Connect(config);
        pair.offerer->Close();
        Require(pair.offerer->State() == WebRtcState::Closed && pair.offerer->CloseReason() == WebRtcCloseReason::Local, "a local close was not reported");
        Require(Pump(config.timeoutMilliseconds, [&] { pair.Update(); }, [&] { return pair.answerer->State() == WebRtcState::Closed; }),
            "the peer did not notice the close");
        Require(pair.answerer->Send(WebRtcChannel::Reliable, Payload(1U, 16U)) == TransportStatus::Error, "a closed link accepted a message");
    }
    {
        // A peer that stops polling stops sending keep-alives; the other side times out.
        LinkPair pair = Connect(config);
        const std::uint64_t start = Millis();
        Require(Pump(config.timeoutMilliseconds + 1000U, [&] { pair.answerer->Update(); }, [&] { return pair.answerer->State() == WebRtcState::Closed; }),
            "a silent peer was never dropped");
        Require(Millis() - start <= config.timeoutMilliseconds + 500U, "the timeout took longer than configured");
        Require(pair.answerer->CloseReason() == WebRtcCloseReason::Timeout || pair.answerer->CloseReason() == WebRtcCloseReason::Remote,
            "the wrong close reason");
    }
    {
        // A link that is never answered gives up after the connect timeout.
        auto [first, second] = CreateSignalingPair();
        WebRtcConfig impatient = config;
        impatient.connectTimeoutMilliseconds = 700U;
        std::string error;
        auto lonely = CreateWebRtcOfferer(std::move(first), impatient, error);
        Require(lonely != nullptr, error);
        Require(Pump(3000U, [&] { lonely->Update(); }, [&] { return lonely->State() == WebRtcState::Closed; }), "an unanswered offer never timed out");
        Require(lonely->CloseReason() == WebRtcCloseReason::ConnectTimeout, "the wrong close reason for an unanswered offer");
    }
}

SessionKey MakeKey(std::uint8_t seed) {
    SessionKey key{};
    for (std::size_t index = 0U; index < key.size(); ++index) key[index] = static_cast<std::uint8_t>(seed + index * 5U);
    return key;
}

// Runs a host and its clients in real time until `done` or the limit.
bool RunSessions(Host& host, std::vector<Client*> clients, std::uint64_t limitMilliseconds, const std::function<void()>& observe,
    const std::function<bool()>& done) {
    return Pump(limitMilliseconds, [&] {
        const std::uint64_t now = Millis();
        host.Update(now);
        for (Client* client : clients) client->Update(now);
        observe();
    }, done);
}

// One host over `serverTransport` serves a WebRTC client and, when `udpPeer` is set, a UDP client as well.
void RunSessionOverWebRtc(IDatagramTransport& serverTransport, std::unique_ptr<IDatagramTransport> webRtcClient, const Endpoint& udpPeer) {
    StaticKeyTable keys;
    keys.Set(1U, MakeKey(1U));
    keys.Set(2U, MakeKey(2U));
    Host host(serverTransport, HostConfig{}, keys);
    Require(host.Valid(), "the host is not valid");
    ClientConfig clientConfig;
    clientConfig.connectTimeoutMilliseconds = 30'000U;
    const std::uint64_t start = Millis();
    Client rtcClient(*webRtcClient, clientConfig, 1U, MakeKey(1U), Endpoint::Loopback(9000), start);
    std::string error;
    auto udpTransport = udpPeer.port != 0U ? CreateUdpTransport(Endpoint::Loopback(0U), error) : nullptr;
    std::unique_ptr<Client> udpClient;
    std::vector<Client*> clients{ &rtcClient };
    if (udpTransport != nullptr) {
        udpClient = std::make_unique<Client>(*udpTransport, clientConfig, 2U, MakeKey(2U), udpPeer, start);
        clients.push_back(udpClient.get());
    }
    std::size_t hostGot[2]{};
    std::size_t clientGot[2]{};
    NetEvent event;
    const auto observe = [&] {
        while (host.PollEvent(event)) {
            if (event.type == NetEventType::Message) {
                Require(event.message.data == Payload(static_cast<std::uint32_t>(event.peer), 300U), "a client message arrived damaged");
                ++hostGot[event.peer - 1U];
            }
        }
        for (std::size_t index = 0U; index < clients.size(); ++index) {
            while (clients[index]->PollEvent(event)) {
                if (event.type == NetEventType::Message) {
                    Require(event.message.data == Payload(9U, 5000U), "a host message arrived damaged");
                    ++clientGot[index];
                }
            }
        }
    };
    Require(RunSessions(host, clients, 30'000U, observe, [&] {
        return host.ClientCount() == clients.size() && rtcClient.State() == ClientState::Connected;
    }), "the clients did not connect through the host");
    // A reliable message over several fragments, and an unreliable one, each way.
    for (std::uint64_t id = 1U; id <= clients.size(); ++id) {
        Require(host.Send(id, 0U, Payload(9U, 5000U)) == SendResult::Queued, "the host could not send");
        Require(clients[id - 1U]->Send(0U, Payload(static_cast<std::uint32_t>(id), 300U)) == SendResult::Queued, "a client could not send");
    }
    Require(RunSessions(host, clients, 15'000U, observe, [&] {
        return hostGot[0] == 1U && clientGot[0] == 1U && (clients.size() == 1U || (hostGot[1] == 1U && clientGot[1] == 1U));
    }), "the session messages did not arrive");
    Require(host.Stats().handshakesRejected == 0U, "a handshake was rejected");
}

void TestSessionOverWebRtc() {
    auto [clientEnd, serverEnd] = CreateSignalingPair();
    auto listener = std::make_shared<SignalingListener>();
    listener->Push(std::move(serverEnd));
    WebRtcServerOptions options;
    options.link = LocalConfig();
    std::string error;
    auto server = CreateWebRtcServerTransport(options, listener, nullptr, error);
    Require(server != nullptr, error);
    auto client = CreateWebRtcClientTransport(std::move(clientEnd), LocalConfig(), Endpoint::Loopback(9000), error);
    Require(client != nullptr, error);
    RunSessionOverWebRtc(*server, std::move(client), Endpoint{});
}

#if defined(KB_NET_WEBSOCKET_SERVER)
// The dedicated server's shape: one listener whose WebSocket serves signaling, with UDP and WebRTC peers on one host.
void TestSessionWithWebSocketSignaling() {
    auto listener = std::make_shared<SignalingListener>();
    WebSocketServerOptions web;
    web.udpBind = Endpoint::Loopback(0U);
    web.maxClients = 4U;
    web.signaling = listener;
    std::uint16_t webSocketPort = 0U;
    std::string error;
    auto webServer = CreateWebSocketServerTransport(web, webSocketPort, error);
    Require(webServer != nullptr, error);
    const Endpoint udp = webServer->LocalEndpoint();
    WebRtcServerOptions options;
    options.link = LocalConfig();
    auto server = CreateWebRtcServerTransport(options, listener, std::move(webServer), error);
    Require(server != nullptr, error);
    auto signaling = CreateWebSocketSignalingClient("ws://127.0.0.1:" + std::to_string(webSocketPort) + std::string(kSignalingWebSocketPath), error);
    Require(signaling != nullptr, error);
    auto client = CreateWebRtcClientTransport(std::move(signaling), LocalConfig(), Endpoint::Loopback(9000), error);
    Require(client != nullptr, error);
    RunSessionOverWebRtc(*server, std::move(client), udp);
}
#endif

} // namespace

int main() {
    const std::vector<std::pair<const char*, void (*)()>> tests{
        { "webrtc-messages", &TestMessages },
        { "webrtc-large-message", &TestLargeMessage },
        { "webrtc-disconnect", &TestDisconnect },
        { "webrtc-session", &TestSessionOverWebRtc },
#if defined(KB_NET_WEBSOCKET_SERVER)
        { "webrtc-websocket-signaling", &TestSessionWithWebSocketSignaling },
#endif
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
