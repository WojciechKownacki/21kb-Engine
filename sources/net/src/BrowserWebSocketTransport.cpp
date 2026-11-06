#include "kb/net/WebSocketTransport.hpp"

#if defined(__EMSCRIPTEN__)
#include <emscripten/websocket.h>

#include <algorithm>
#include <array>
#include <cstring>

EM_JS_DEPS(kb_datagram_socket_dependencies, "emscripten_websocket_new");
EM_JS(int, KbCreateDatagramSocket, (EmscriptenWebSocketCreateAttributes* attributes), {
    try { return _emscripten_websocket_new(attributes); }
    catch (error) { return 0; }
});

namespace kb::net {
namespace {

class BrowserWebSocketTransport final : public IDatagramTransport {
public:
    explicit BrowserWebSocketTransport(Endpoint peer) : peer_(peer) {}
    ~BrowserWebSocketTransport() override {
        if (socket_ <= 0) return;
        static_cast<void>(emscripten_websocket_set_onopen_callback(socket_, nullptr, nullptr));
        static_cast<void>(emscripten_websocket_set_onmessage_callback(socket_, nullptr, nullptr));
        static_cast<void>(emscripten_websocket_set_onerror_callback(socket_, nullptr, nullptr));
        static_cast<void>(emscripten_websocket_set_onclose_callback(socket_, nullptr, nullptr));
        static_cast<void>(emscripten_websocket_close(socket_, 1000U, "session ended"));
        static_cast<void>(emscripten_websocket_delete(socket_));
    }

    bool Open(const std::string& url) {
        EmscriptenWebSocketCreateAttributes attributes{};
        attributes.url = url.c_str();
        attributes.protocols = kDatagramWebSocketProtocol;
        attributes.createOnMainThread = false;
        socket_ = KbCreateDatagramSocket(&attributes);
        return socket_ > 0 &&
            emscripten_websocket_set_onopen_callback(socket_, this, OnOpen) == EMSCRIPTEN_RESULT_SUCCESS &&
            emscripten_websocket_set_onmessage_callback(socket_, this, OnMessage) == EMSCRIPTEN_RESULT_SUCCESS &&
            emscripten_websocket_set_onerror_callback(socket_, this, OnError) == EMSCRIPTEN_RESULT_SUCCESS &&
            emscripten_websocket_set_onclose_callback(socket_, this, OnClose) == EMSCRIPTEN_RESULT_SUCCESS;
    }

    TransportStatus Send(const Endpoint& to, std::span<const std::uint8_t> datagram) override {
        if (datagram.size() > kMaxDatagramBytes) return TransportStatus::TooLarge;
        if (failed_ || to != peer_) return TransportStatus::Error;
        if (!ready_) return TransportStatus::WouldBlock;
        std::size_t buffered = 0U;
        if (emscripten_websocket_get_buffered_amount(socket_, &buffered) != EMSCRIPTEN_RESULT_SUCCESS)
            return TransportStatus::Error;
        if (buffered > kQueueCapacity * kMaxDatagramBytes - datagram.size()) return TransportStatus::WouldBlock;
        std::uint8_t empty = 0U;
        return emscripten_websocket_send_binary(socket_, datagram.empty() ? &empty : const_cast<std::uint8_t*>(datagram.data()),
            static_cast<std::uint32_t>(datagram.size())) == EMSCRIPTEN_RESULT_SUCCESS ?
            TransportStatus::Ok : TransportStatus::Error;
    }

    TransportStatus Receive(Endpoint& from, std::span<std::uint8_t> buffer, std::size_t& size) override {
        size = 0U;
        if (failed_) return TransportStatus::Error;
        if (count_ == 0U) return TransportStatus::WouldBlock;
        const Packet& packet = inbox_[head_];
        size = std::min(buffer.size(), packet.size);
        std::copy_n(packet.bytes.begin(), size, buffer.begin());
        from = peer_;
        head_ = (head_ + 1U) % kQueueCapacity;
        --count_;
        return TransportStatus::Ok;
    }

    Endpoint LocalEndpoint() const noexcept override { return Endpoint::Loopback(0U); }

private:
    static bool OnOpen(int, const EmscriptenWebSocketOpenEvent*, void* context) {
        auto& self = *static_cast<BrowserWebSocketTransport*>(context);
        std::array<char, 64U> protocol{};
        if (emscripten_websocket_get_protocol(self.socket_, protocol.data(), static_cast<int>(protocol.size())) !=
                EMSCRIPTEN_RESULT_SUCCESS || std::strcmp(protocol.data(), kDatagramWebSocketProtocol) != 0) {
            self.Fail(1002U, "session protocol mismatch");
        } else {
            self.ready_ = true;
        }
        return true;
    }

    static bool OnMessage(int, const EmscriptenWebSocketMessageEvent* event, void* context) {
        auto& self = *static_cast<BrowserWebSocketTransport*>(context);
        if (self.failed_) return true;
        if (event->isText || event->numBytes > kMaxDatagramBytes || self.count_ == kQueueCapacity) {
            self.Fail(event->isText ? 1003U : 1009U, "invalid session datagram");
            return true;
        }
        Packet& packet = self.inbox_[(self.head_ + self.count_) % kQueueCapacity];
        packet.size = event->numBytes;
        std::copy_n(event->data, packet.size, packet.bytes.begin());
        ++self.count_;
        return true;
    }

    static bool OnError(int, const EmscriptenWebSocketErrorEvent*, void* context) {
        static_cast<BrowserWebSocketTransport*>(context)->failed_ = true;
        return true;
    }
    static bool OnClose(int, const EmscriptenWebSocketCloseEvent*, void* context) {
        static_cast<BrowserWebSocketTransport*>(context)->failed_ = true;
        return true;
    }
    void Fail(unsigned short code, const char* reason) {
        failed_ = true;
        static_cast<void>(emscripten_websocket_close(socket_, code, reason));
    }

    static constexpr std::size_t kQueueCapacity = 256U;
    struct Packet { std::array<std::uint8_t, kMaxDatagramBytes> bytes{}; std::size_t size = 0U; };
    std::array<Packet, kQueueCapacity> inbox_{};
    Endpoint peer_;
    EMSCRIPTEN_WEBSOCKET_T socket_ = 0;
    std::size_t head_ = 0U;
    std::size_t count_ = 0U;
    bool ready_ = false;
    bool failed_ = false;
};
} // namespace

std::unique_ptr<IDatagramTransport> CreateBrowserWebSocketTransport(
    const std::string& url, Endpoint peer, std::string& error) {
    error.clear();
    if ((!url.starts_with("ws://") && !url.starts_with("wss://")) || url.size() > 2048U ||
        url.find_first_of("\r\n") != std::string::npos || url.find('\0') != std::string::npos ||
        !emscripten_websocket_is_supported()) {
        error = "a supported browser and a ws:// or wss:// endpoint are required";
        return nullptr;
    }
    auto transport = std::make_unique<BrowserWebSocketTransport>(peer);
    if (!transport->Open(url)) {
        error = "the browser could not create the session WebSocket";
        return nullptr;
    }
    return transport;
}
} // namespace kb::net
#else
namespace kb::net {
std::unique_ptr<IDatagramTransport> CreateBrowserWebSocketTransport(const std::string&, Endpoint, std::string& error) {
    error = "browser WebSocket transport requires an Emscripten build";
    return nullptr;
}
} // namespace kb::net
#endif
