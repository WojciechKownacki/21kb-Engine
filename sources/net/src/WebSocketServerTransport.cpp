#include "kb/net/WebRtcTransport.hpp"
#include "kb/net/WebSocketTransport.hpp"

#if defined(KB_NET_WEBSOCKET_SERVER)
#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <libwebsockets.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <deque>
#include <exception>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <thread>

namespace kb::net {
namespace {

constexpr std::size_t kMaxSignalingBytes = 64U * 1024U;
constexpr std::size_t kMaxSignalingQueue = 64U;

// The state one signaling connection shares between the listener thread and the code that owns the channel.
class SignalShared {
public:
    explicit SignalShared(lws_context* context) : context_(context) {}

    bool Send(std::string message) {
        std::lock_guard lock(mutex_);
        if (closed_ || outbound_.size() >= kMaxSignalingQueue || message.size() > kMaxSignalingBytes) return false;
        outbound_.push_back(std::move(message));
        Wake();
        return true;
    }
    SignalingStatus Receive(std::string& message) {
        std::lock_guard lock(mutex_);
        if (!inbound_.empty()) {
            message = std::move(inbound_.front());
            inbound_.pop_front();
            return SignalingStatus::Ok;
        }
        return closed_ ? SignalingStatus::Closed : SignalingStatus::WouldBlock;
    }
    void Close() {
        std::lock_guard lock(mutex_);
        closeRequested_ = true;
        Wake();
    }
    // Listener thread: the connection is gone.
    void Detach() {
        std::lock_guard lock(mutex_);
        closed_ = true;
        context_ = nullptr;
    }
    bool PushInbound(std::string message) {
        std::lock_guard lock(mutex_);
        if (inbound_.size() >= kMaxSignalingQueue) return false;
        inbound_.push_back(std::move(message));
        return true;
    }
    bool HasWork() {
        std::lock_guard lock(mutex_);
        return !outbound_.empty() || closeRequested_;
    }
    // Next message to write; `close` is set when the owner closed the channel and nothing is left to send.
    bool PopOutbound(std::string& message, bool& more, bool& close) {
        std::lock_guard lock(mutex_);
        more = false;
        close = false;
        if (outbound_.empty()) {
            close = closeRequested_;
            return false;
        }
        message = std::move(outbound_.front());
        outbound_.pop_front();
        more = !outbound_.empty() || closeRequested_;
        return true;
    }

private:
    void Wake() {
        if (context_ != nullptr) lws_cancel_service(context_);
    }

    std::mutex mutex_;
    std::deque<std::string> inbound_;
    std::deque<std::string> outbound_;
    lws_context* context_;
    bool closed_ = false;
    bool closeRequested_ = false;
};

class SignalHandle final : public ISignalingChannel {
public:
    explicit SignalHandle(std::shared_ptr<SignalShared> shared) : shared_(std::move(shared)) {}
    ~SignalHandle() override { shared_->Close(); }
    bool Send(std::string message) override { return shared_->Send(std::move(message)); }
    SignalingStatus Receive(std::string& message) override { return shared_->Receive(message); }
    void Close() override { shared_->Close(); }

private:
    std::shared_ptr<SignalShared> shared_;
};

class WebSocketServerTransport final : public IDatagramTransport {
public:
    WebSocketServerTransport(WebSocketServerOptions options, std::unique_ptr<IDatagramTransport> udp)
        : options_(std::move(options)), udp_(std::move(udp)) {}

    ~WebSocketServerTransport() override {
        stopping_.store(true);
        if (context_ != nullptr) lws_cancel_service(context_);
        if (worker_.joinable()) worker_.join();
        if (context_ != nullptr) lws_context_destroy(context_);
    }

    bool Open(std::uint16_t& port, std::string& error) {
        protocols_[0].name = "http";
        protocols_[0].callback = Callback;
        protocols_[1].name = kDatagramWebSocketProtocol;
        protocols_[1].callback = Callback;
        protocols_[1].per_session_data_size = sizeof(PeerState);
        protocols_[1].rx_buffer_size = kMaxDatagramBytes;
        protocols_[1].tx_packet_size = kMaxDatagramBytes;
        if (options_.signaling != nullptr) {
            protocols_[2].name = kSignalingWebSocketProtocol;
            protocols_[2].callback = Callback;
            protocols_[2].per_session_data_size = sizeof(SignalState);
            protocols_[2].rx_buffer_size = 4096U;
            protocols_[2].tx_packet_size = kMaxSignalingBytes;
        }
        const std::size_t connections = options_.signaling != nullptr ? 2U * options_.maxClients : options_.maxClients;
        address_ = options_.udpBind.ToString();
        address_.resize(address_.rfind(':'));
        lws_context_creation_info info{};
        info.port = options_.webSocketPort;
        info.iface = options_.udpBind.address == 0U ? nullptr : address_.c_str();
        info.protocols = protocols_.data();
        info.user = this;
        info.gid = static_cast<decltype(info.gid)>(-1);
        info.uid = static_cast<decltype(info.uid)>(-1);
        info.fd_limit_per_thread = static_cast<unsigned int>(connections + 4U);
        info.max_http_header_data = 4096U;
        info.max_http_header_pool = static_cast<unsigned short>(connections);
        info.timeout_secs = 5U;
        info.pt_serv_buf_size = 4096U;
        info.vhost_name = "21kb-session";
        context_ = lws_create_context(&info);
        if (context_ == nullptr) { error = "the WebSocket listener could not be created"; return false; }
        lws_vhost* vhost = lws_get_vhost_by_name(context_, info.vhost_name);
        const int actualPort = vhost != nullptr ? lws_get_vhost_listen_port(vhost) : 0;
        if (actualPort <= 0 || actualPort > 65535) { error = "the WebSocket listener has no bound port"; return false; }
        port = static_cast<std::uint16_t>(actualPort);
        worker_ = std::thread([this] {
            while (!stopping_.load()) {
                if (lws_service(context_, 0) < 0) { failed_.store(true); break; }
            }
        });
        return true;
    }

    TransportStatus Send(const Endpoint& to, std::span<const std::uint8_t> datagram) override {
        if (!Virtual(to)) return udp_->Send(to, datagram);
        if (datagram.size() > kMaxDatagramBytes) return TransportStatus::TooLarge;
        if (failed_.load()) return TransportStatus::Error;
        {
            std::lock_guard lock(mutex_);
            const auto found = peers_.find(to);
            if (found == peers_.end()) return TransportStatus::Error;
            if (queuedSends_ == kGlobalQueueLimit || found->second.outbound.size() == kPeerQueueLimit)
                return TransportStatus::WouldBlock;
            Packet packet{};
            packet.size = datagram.size();
            std::copy(datagram.begin(), datagram.end(), packet.bytes.begin());
            found->second.outbound.push_back(packet);
            ++queuedSends_;
        }
        lws_cancel_service(context_);
        return TransportStatus::Ok;
    }

    TransportStatus Receive(Endpoint& from, std::span<std::uint8_t> buffer, std::size_t& size) override {
        preferWeb_ = !preferWeb_;
        if (preferWeb_ && ReceiveWeb(from, buffer, size)) return TransportStatus::Ok;
        const auto status = udp_->Receive(from, buffer, size);
        if (status == TransportStatus::Ok && !Virtual(from)) return status;
        if (ReceiveWeb(from, buffer, size)) return TransportStatus::Ok;
        size = 0U;
        return status == TransportStatus::Error || failed_.load() ? TransportStatus::Error : TransportStatus::WouldBlock;
    }

    Endpoint LocalEndpoint() const override { return udp_->LocalEndpoint(); }
    bool WaitReadable(std::uint32_t milliseconds) override {
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
        for (;;) {
            if (udp_->WaitReadable(0U)) return true;
            std::unique_lock lock(mutex_);
            if (!inbox_.empty() || failed_.load()) return true;
            if (std::chrono::steady_clock::now() >= until) return false;
            readable_.wait_until(lock, std::min(until, std::chrono::steady_clock::now() + std::chrono::milliseconds(1)));
        }
    }

private:
    static constexpr std::size_t kPeerQueueLimit = 256U;
    static constexpr std::size_t kGlobalQueueLimit = 4096U;
    struct Packet {
        Endpoint from{};
        std::size_t size = 0U;
        std::array<std::uint8_t, kMaxDatagramBytes> bytes{};
    };
    struct PeerState { std::uint64_t id; std::size_t received; std::uint8_t bytes[kMaxDatagramBytes]; };
    struct SignalState { struct Impl { std::shared_ptr<SignalShared> shared; std::string partial; }* impl; };
    struct Peer { lws* socket = nullptr; std::deque<Packet> outbound; };

    static bool Virtual(Endpoint endpoint) { return (endpoint.address & 0xFF000000U) == 0U; }
    static Endpoint Identity(std::uint64_t id) {
        return {static_cast<std::uint32_t>(id >> 16U), static_cast<std::uint16_t>(id & 0xFFFFU)};
    }

    bool ReceiveWeb(Endpoint& from, std::span<std::uint8_t> buffer, std::size_t& size) {
        std::lock_guard lock(mutex_);
        if (inbox_.empty()) return false;
        const auto& packet = inbox_.front();
        from = packet.from;
        size = std::min(buffer.size(), packet.size);
        std::copy_n(packet.bytes.begin(), size, buffer.begin());
        inbox_.pop_front();
        return true;
    }

    static int Callback(lws* socket, lws_callback_reasons reason, void* user, void* input, std::size_t length) {
        if (socket == nullptr) return 0;
        auto* self = static_cast<WebSocketServerTransport*>(lws_context_user(lws_get_context(socket)));
        if (self == nullptr) return 0;
        const lws_protocols* protocol = lws_get_protocol(socket);
        const bool signal = protocol != nullptr && std::strcmp(protocol->name, kSignalingWebSocketProtocol) == 0;
        try {
            if (signal && reason != LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION)
                return self->DispatchSignal(socket, reason, static_cast<SignalState*>(user), input, length);
            return self->Dispatch(socket, reason, static_cast<PeerState*>(user), input, length);
        }
        catch (...) { return -1; }
    }

    int Dispatch(lws* socket, lws_callback_reasons reason, PeerState* state, void* input, std::size_t length) {
        switch (reason) {
        case LWS_CALLBACK_HTTP:
            return -1; // This listener does not serve project assets or files.
        case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION: {
            const auto* protocol = lws_get_protocol(socket);
            const bool signal = protocol != nullptr && std::strcmp(protocol->name, kSignalingWebSocketProtocol) == 0;
            if (protocol == nullptr || (!signal && std::strcmp(protocol->name, kDatagramWebSocketProtocol) != 0)) return 1;
            std::array<char, 2049U> header{};
            if (lws_hdr_copy(socket, header.data(), static_cast<int>(header.size()), WSI_TOKEN_GET_URI) < 0 ||
                std::strcmp(header.data(), signal ? kSignalingWebSocketPath : "/21kb") != 0) return 1;
            if (!options_.allowedOrigin.empty()) {
                if (lws_hdr_copy(socket, header.data(), static_cast<int>(header.size()), WSI_TOKEN_ORIGIN) < 0 ||
                    header.data() != options_.allowedOrigin) return 1;
            }
            std::lock_guard lock(mutex_);
            return (signal ? signals_.size() : peers_.size()) >= options_.maxClients ? 1 : 0;
        }
        case LWS_CALLBACK_ESTABLISHED: {
            if (state == nullptr) return -1;
            std::lock_guard lock(mutex_);
            // Never recycle an identity while old host cookies or sessions may exist. WebRTC peers own the upper half.
            if (peers_.size() >= options_.maxClients || nextId_ >= kWebRtcIdentityBase) return -1;
            *state = PeerState{};
            state->id = nextId_++;
            peers_.emplace(Identity(state->id), Peer{socket, {}});
            return 0;
        }
        case LWS_CALLBACK_RECEIVE: {
            if (state == nullptr || state->id == 0U || !lws_frame_is_binary(socket) ||
                length > kMaxDatagramBytes - state->received ||
                lws_remaining_packet_payload(socket) > kMaxDatagramBytes - state->received - length) return -1;
            if (length != 0U) std::memcpy(state->bytes + state->received, input, length);
            state->received += length;
            if (lws_is_final_fragment(socket) && lws_remaining_packet_payload(socket) == 0U) {
                Packet packet{};
                packet.from = Identity(state->id);
                packet.size = state->received;
                std::copy_n(state->bytes, packet.size, packet.bytes.begin());
                std::lock_guard lock(mutex_);
                if (inbox_.size() == kGlobalQueueLimit) return -1;
                inbox_.push_back(packet);
                readable_.notify_all();
                state->received = 0U;
            }
            return 0;
        }
        case LWS_CALLBACK_EVENT_WAIT_CANCELLED: {
            std::lock_guard lock(mutex_);
            for (const auto& [endpoint, peer] : peers_) {
                static_cast<void>(endpoint);
                if (!peer.outbound.empty()) lws_callback_on_writable(peer.socket);
            }
            for (const auto& [wsi, shared] : signals_) {
                if (shared->HasWork()) lws_callback_on_writable(wsi);
            }
            return 0;
        }
        case LWS_CALLBACK_SERVER_WRITEABLE: {
            if (state == nullptr || state->id == 0U) return -1;
            Packet packet{};
            bool more = false;
            {
                std::lock_guard lock(mutex_);
                const auto found = peers_.find(Identity(state->id));
                if (found == peers_.end() || found->second.outbound.empty()) return 0;
                packet = found->second.outbound.front();
                found->second.outbound.pop_front();
                --queuedSends_;
                more = !found->second.outbound.empty();
            }
            std::array<unsigned char, LWS_PRE + kMaxDatagramBytes> data{};
            std::copy_n(packet.bytes.begin(), packet.size, data.begin() + LWS_PRE);
            if (lws_write(socket, data.data() + LWS_PRE, packet.size, LWS_WRITE_BINARY) != static_cast<int>(packet.size)) return -1;
            if (more) lws_callback_on_writable(socket);
            return 0;
        }
        case LWS_CALLBACK_CLOSED: {
            if (state == nullptr || state->id == 0U) return 0;
            std::lock_guard lock(mutex_);
            const auto found = peers_.find(Identity(state->id));
            if (found != peers_.end()) {
                queuedSends_ -= found->second.outbound.size();
                peers_.erase(found);
            }
            return 0;
        }
        default:
            return 0;
        }
    }

    int DispatchSignal(lws* socket, lws_callback_reasons reason, SignalState* state, void* input, std::size_t length) {
        switch (reason) {
        case LWS_CALLBACK_ESTABLISHED: {
            if (state == nullptr) return -1;
            std::lock_guard lock(mutex_);
            if (signals_.size() >= options_.maxClients) return -1;
            auto shared = std::make_shared<SignalShared>(context_);
            state->impl = new SignalState::Impl{ shared, {} };
            signals_.emplace(socket, shared);
            options_.signaling->Push(std::make_unique<SignalHandle>(std::move(shared)));
            return 0;
        }
        case LWS_CALLBACK_RECEIVE: {
            if (state == nullptr || state->impl == nullptr || lws_frame_is_binary(socket)) return -1;
            std::string& partial = state->impl->partial;
            if (length > kMaxSignalingBytes - partial.size()) return -1;
            partial.append(static_cast<const char*>(input), length);
            if (lws_is_final_fragment(socket) && lws_remaining_packet_payload(socket) == 0U) {
                if (!state->impl->shared->PushInbound(std::move(partial))) return -1;
                partial.clear();
            }
            return 0;
        }
        case LWS_CALLBACK_SERVER_WRITEABLE: {
            if (state == nullptr || state->impl == nullptr) return -1;
            std::string message;
            bool more = false;
            bool close = false;
            if (!state->impl->shared->PopOutbound(message, more, close)) return close ? -1 : 0;
            std::vector<unsigned char> data(LWS_PRE + message.size());
            std::copy(message.begin(), message.end(), data.begin() + LWS_PRE);
            if (lws_write(socket, data.data() + LWS_PRE, message.size(), LWS_WRITE_TEXT) != static_cast<int>(message.size())) return -1;
            if (more) lws_callback_on_writable(socket);
            return 0;
        }
        case LWS_CALLBACK_CLOSED: {
            if (state == nullptr || state->impl == nullptr) return 0;
            state->impl->shared->Detach();
            std::lock_guard lock(mutex_);
            signals_.erase(socket);
            delete state->impl;
            state->impl = nullptr;
            return 0;
        }
        default:
            return 0;
        }
    }

    WebSocketServerOptions options_;
    std::unique_ptr<IDatagramTransport> udp_;
    std::string address_;
    std::array<lws_protocols, 4U> protocols_{};
    lws_context* context_ = nullptr;
    std::thread worker_;
    std::atomic<bool> stopping_{false};
    std::atomic<bool> failed_{false};
    std::mutex mutex_;
    std::condition_variable readable_;
    std::map<Endpoint, Peer> peers_;
    std::map<lws*, std::shared_ptr<SignalShared>> signals_;
    std::deque<Packet> inbox_;
    std::uint64_t nextId_ = 1U;
    std::size_t queuedSends_ = 0U;
    bool preferWeb_ = false;
};
} // namespace

std::unique_ptr<IDatagramTransport> CreateWebSocketServerTransport(
    const WebSocketServerOptions& options, std::uint16_t& webSocketPort, std::string& error) {
    webSocketPort = 0U;
    error.clear();
    if (options.maxClients == 0U || options.maxClients > 4096U || options.allowedOrigin.size() > 2048U) {
        error = "invalid WebSocket listener limits";
        return nullptr;
    }
    auto udp = CreateUdpTransport(options.udpBind, error);
    if (udp == nullptr) return nullptr;
    try {
        auto transport = std::make_unique<WebSocketServerTransport>(options, std::move(udp));
        if (transport->Open(webSocketPort, error)) return transport;
    } catch (const std::exception&) {
        error = "the WebSocket service could not be started";
    }
    return nullptr;
}
} // namespace kb::net
#else
namespace kb::net {
std::unique_ptr<IDatagramTransport> CreateWebSocketServerTransport(
    const WebSocketServerOptions&, std::uint16_t& port, std::string& error) {
    port = 0U;
    error = "WebSocket server support was not enabled in this build";
    return nullptr;
}
} // namespace kb::net
#endif
