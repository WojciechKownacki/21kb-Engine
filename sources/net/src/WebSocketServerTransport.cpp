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
#include <exception>
#include <mutex>
#include <thread>

namespace kb::net {
namespace {

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
        address_ = options_.udpBind.ToString();
        address_.resize(address_.rfind(':'));
        lws_context_creation_info info{};
        info.port = options_.webSocketPort;
        info.iface = options_.udpBind.address == 0U ? nullptr : address_.c_str();
        info.protocols = protocols_.data();
        info.user = this;
        info.gid = static_cast<decltype(info.gid)>(-1);
        info.uid = static_cast<decltype(info.uid)>(-1);
        info.fd_limit_per_thread = static_cast<unsigned int>(options_.maxClients + 4U);
        info.max_http_header_data = 4096U;
        info.max_http_header_pool = static_cast<unsigned short>(options_.maxClients);
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

private:
    static constexpr std::size_t kPeerQueueLimit = 256U;
    static constexpr std::size_t kGlobalQueueLimit = 4096U;
    struct Packet {
        Endpoint from{};
        std::size_t size = 0U;
        std::array<std::uint8_t, kMaxDatagramBytes> bytes{};
    };
    struct PeerState { std::uint64_t id; std::size_t received; std::uint8_t bytes[kMaxDatagramBytes]; };
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
        try { return self->Dispatch(socket, reason, static_cast<PeerState*>(user), input, length); }
        catch (...) { return -1; }
    }

    int Dispatch(lws* socket, lws_callback_reasons reason, PeerState* state, void* input, std::size_t length) {
        switch (reason) {
        case LWS_CALLBACK_HTTP:
            return -1; // This listener does not serve project assets or files.
        case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION: {
            const auto* protocol = lws_get_protocol(socket);
            if (protocol == nullptr || std::strcmp(protocol->name, kDatagramWebSocketProtocol) != 0) return 1;
            std::array<char, 2049U> header{};
            if (lws_hdr_copy(socket, header.data(), static_cast<int>(header.size()), WSI_TOKEN_GET_URI) < 0 ||
                std::strcmp(header.data(), "/21kb") != 0) return 1;
            if (!options_.allowedOrigin.empty()) {
                if (lws_hdr_copy(socket, header.data(), static_cast<int>(header.size()), WSI_TOKEN_ORIGIN) < 0 ||
                    header.data() != options_.allowedOrigin) return 1;
            }
            std::lock_guard lock(mutex_);
            return peers_.size() >= options_.maxClients ? 1 : 0;
        }
        case LWS_CALLBACK_ESTABLISHED: {
            if (state == nullptr) return -1;
            std::lock_guard lock(mutex_);
            // Never recycle an identity while old host cookies or sessions may exist.
            if (peers_.size() >= options_.maxClients || nextId_ >= (1ULL << 40U)) return -1;
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

    WebSocketServerOptions options_;
    std::unique_ptr<IDatagramTransport> udp_;
    std::string address_;
    std::array<lws_protocols, 3U> protocols_{};
    lws_context* context_ = nullptr;
    std::thread worker_;
    std::atomic<bool> stopping_{false};
    std::atomic<bool> failed_{false};
    std::mutex mutex_;
    std::map<Endpoint, Peer> peers_;
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
