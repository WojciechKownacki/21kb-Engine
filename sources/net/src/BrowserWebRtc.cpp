// WebRTC links in the browser: RTCPeerConnection through small EM_JS functions, and a WebSocket
// signaling client. The browser can only offer.

#include "kb/net/WebRtcTransport.hpp"
#include "kb/net/WebSocketTransport.hpp"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <emscripten/websocket.h>

#include <array>
#include <deque>

EM_JS(int, KbRtcOpen, (const char* servers), {
    try {
        const state = Module.kbRtc || (Module.kbRtc = { next: 1, links: new Map() });
        const iceServers = [];
        for (const entry of UTF8ToString(servers).split('\n')) {
            if (!entry) continue;
            const match = /^(stuns?|turns?):(?:([^:@\/]+):([^@\/]+)@)?(.+)$/.exec(entry);
            if (!match) return 0;
            const server = { urls: match[1] + ':' + match[4] };
            if (match[2]) { server.username = decodeURIComponent(match[2]); server.credential = decodeURIComponent(match[3]); }
            iceServers.push(server);
        }
        const link = { pc: new RTCPeerConnection({ iceServers: iceServers }), channels: [], inbox: [[], []], signals: [], chain: Promise.resolve(), state: 0, last: performance.now() };
        const fail = () => { link.state = 2; };
        link.pc.onicecandidate = (event) => {
            if (event.candidate) link.signals.push('candidate\n' + (event.candidate.sdpMid || '0') + '\n' + event.candidate.candidate);
        };
        link.pc.onconnectionstatechange = () => {
            if (link.pc.connectionState === 'failed' || link.pc.connectionState === 'closed' || link.pc.connectionState === 'disconnected') fail();
        };
        const names = ['reliable', 'unreliable'];
        for (let index = 0; index < 2; ++index) {
            const channel = link.pc.createDataChannel(names[index], index === 0 ? {} : { ordered: false, maxRetransmits: 0 });
            channel.binaryType = 'arraybuffer';
            channel.onmessage = (event) => {
                link.last = performance.now();
                if (event.data.byteLength === 0) return;
                if (link.inbox[index].length < 4096) link.inbox[index].push(new Uint8Array(event.data));
                else if (index === 0) fail();
            };
            channel.onclose = fail;
            channel.onerror = fail;
            link.channels.push(channel);
        }
        link.chain = link.pc.createOffer()
            .then((offer) => link.pc.setLocalDescription(offer))
            .then(() => { link.signals.push('offer\n' + link.pc.localDescription.sdp); })
            .catch(fail);
        const id = state.next++;
        state.links.set(id, link);
        return id;
    } catch (error) {
        return 0;
    }
});

// 0 connecting, 1 open, 2 closed.
EM_JS(int, KbRtcState, (int id), {
    const link = Module.kbRtc && Module.kbRtc.links.get(id);
    if (!link) return 2;
    if (link.state === 0 && link.channels.every((channel) => channel.readyState === 'open')) { link.state = 1; link.last = performance.now(); }
    return link.state;
});

EM_JS(int, KbRtcIdle, (int id), {
    const link = Module.kbRtc && Module.kbRtc.links.get(id);
    return link ? Math.floor(performance.now() - link.last) : 0;
});

EM_JS(int, KbRtcMaxMessage, (int id), {
    const link = Module.kbRtc && Module.kbRtc.links.get(id);
    const limit = link && link.pc.sctp ? link.pc.sctp.maxMessageSize : 0;
    return limit > 0 && limit < 0x7fffffff ? limit : 0x7fffffff;
});

EM_JS(void, KbRtcApply, (int id, const char* kind, const char* body), {
    const link = Module.kbRtc && Module.kbRtc.links.get(id);
    if (!link) return;
    const type = UTF8ToString(kind);
    const text = UTF8ToString(body);
    link.chain = link.chain.then(() => {
        if (type === 'answer') return link.pc.setRemoteDescription({ type: 'answer', sdp: text });
        const split = text.indexOf('\n');
        return link.pc.addIceCandidate({ sdpMid: text.slice(0, split), candidate: text.slice(split + 1) });
    }).catch(() => { link.state = 2; });
});

// Length of the next signaling message written to `buffer`, or 0 when there is none.
EM_JS(int, KbRtcNextSignal, (int id, char* buffer, int capacity), {
    const link = Module.kbRtc && Module.kbRtc.links.get(id);
    if (!link || link.signals.length === 0) return 0;
    const text = link.signals.shift();
    const length = lengthBytesUTF8(text);
    if (length + 1 > capacity) { link.state = 2; return 0; }
    stringToUTF8(text, buffer, capacity);
    return length;
});

// 0 sent, 1 too much is buffered, 2 the channel is not open.
EM_JS(int, KbRtcSend, (int id, int channel, const uint8_t* data, int size, int limit), {
    const link = Module.kbRtc && Module.kbRtc.links.get(id);
    if (!link || link.channels[channel].readyState !== 'open') return 2;
    if (link.channels[channel].bufferedAmount > limit) return 1;
    try { link.channels[channel].send(HEAPU8.slice(data, data + size)); return 0; } catch (error) { return 2; }
});

EM_JS(int, KbRtcPeek, (int id, int channel), {
    const link = Module.kbRtc && Module.kbRtc.links.get(id);
    return link && link.inbox[channel].length > 0 ? link.inbox[channel][0].length : -1;
});

EM_JS(void, KbRtcTake, (int id, int channel, uint8_t* out), {
    HEAPU8.set(Module.kbRtc.links.get(id).inbox[channel].shift(), out);
});

EM_JS(void, KbRtcClose, (int id), {
    const state = Module.kbRtc;
    const link = state && state.links.get(id);
    if (!link) return;
    state.links.delete(id);
    link.state = 2;
    for (const channel of link.channels) channel.onmessage = channel.onclose = channel.onerror = null;
    link.pc.onicecandidate = link.pc.onconnectionstatechange = null;
    try { link.pc.close(); } catch (error) {}
});

EM_JS_DEPS(kb_signaling_socket_dependencies, "emscripten_websocket_new");
EM_JS(int, KbCreateSignalingSocket, (EmscriptenWebSocketCreateAttributes* attributes), {
    try { return _emscripten_websocket_new(attributes); }
    catch (error) { return 0; }
});

namespace kb::net {
namespace {

constexpr std::size_t kMaxSignalingBytes = 64U * 1024U;
constexpr std::size_t kMaxQueued = 64U;
constexpr std::size_t kMaxCandidates = 64U;
constexpr std::size_t kBufferLimit[2] = { 8U * 1024U * 1024U, 256U * 1024U };

class BrowserLink final : public IWebRtcLink {
public:
    BrowserLink(std::unique_ptr<ISignalingChannel> signaling, WebRtcConfig config)
        : signaling_(std::move(signaling)), config_(std::move(config)) {}
    ~BrowserLink() override { Close(); }

    bool Start() {
        std::string servers;
        for (const std::string& server : config_.iceServers) {
            if (server.find_first_of("\n\r") != std::string::npos || server.find('\0') != std::string::npos) return false;
            servers += server + '\n';
        }
        id_ = KbRtcOpen(servers.c_str());
        startedAt_ = emscripten_get_now();
        return id_ > 0;
    }

    void Update() override {
        if (state_ == WebRtcState::Closed) return;
        std::string message;
        while (signaling_->Receive(message) == SignalingStatus::Ok) Apply(message);
        std::array<char, 16U * 1024U> buffer{};
        while (const int length = KbRtcNextSignal(id_, buffer.data(), static_cast<int>(buffer.size())))
            static_cast<void>(signaling_->Send(std::string(buffer.data(), static_cast<std::size_t>(length))));
        const int state = KbRtcState(id_);
        if (state == 2) { Fail(WebRtcCloseReason::Remote); return; }
        const double now = emscripten_get_now();
        if (state_ == WebRtcState::Connecting) {
            if (state == 1) {
                state_ = WebRtcState::Open;
                lastSend_ = now;
                signaling_->Close(); // the peers are connected; nothing more needs to be negotiated
            } else if (now - startedAt_ > config_.connectTimeoutMilliseconds) {
                Fail(WebRtcCloseReason::ConnectTimeout);
            }
            return;
        }
        if (KbRtcIdle(id_) > static_cast<int>(config_.timeoutMilliseconds)) {
            Fail(WebRtcCloseReason::Timeout);
        } else if (now - lastSend_ > config_.timeoutMilliseconds / 4U) {
            static_cast<void>(KbRtcSend(id_, 1, nullptr, 0, 0x7fffffff));
            lastSend_ = now;
        }
    }

    WebRtcState State() const override { return state_; }
    WebRtcCloseReason CloseReason() const override { return reason_; }

    TransportStatus Send(WebRtcChannel channel, std::span<const std::uint8_t> message) override {
        if (state_ == WebRtcState::Closed) return TransportStatus::Error;
        if (state_ == WebRtcState::Connecting) return TransportStatus::WouldBlock;
        const int index = channel == WebRtcChannel::Reliable ? 0 : 1;
        if (message.empty() || message.size() > config_.maxMessageBytes ||
            message.size() > static_cast<std::size_t>(KbRtcMaxMessage(id_))) return TransportStatus::TooLarge;
        const int result = KbRtcSend(id_, index, message.data(), static_cast<int>(message.size()), static_cast<int>(kBufferLimit[index]));
        if (result == 0) lastSend_ = emscripten_get_now();
        return result == 0 ? TransportStatus::Ok : result == 1 ? TransportStatus::WouldBlock : TransportStatus::Error;
    }

    bool Receive(WebRtcChannel channel, std::vector<std::uint8_t>& message) override {
        const int index = channel == WebRtcChannel::Reliable ? 0 : 1;
        const int size = KbRtcPeek(id_, index);
        if (size < 0) return false;
        message.resize(static_cast<std::size_t>(size));
        KbRtcTake(id_, index, message.data());
        return true;
    }

    void Close() override {
        Fail(WebRtcCloseReason::Local);
        signaling_->Close();
        if (id_ > 0) KbRtcClose(id_);
        id_ = 0;
    }

private:
    void Apply(const std::string& message) {
        if (message.size() > kMaxSignalingBytes) return;
        const std::size_t split = message.find('\n');
        if (split == std::string::npos) return;
        const std::string kind = message.substr(0U, split);
        if (kind == "answer" && !haveRemote_) {
            haveRemote_ = true;
        } else if (!(kind == "candidate" && haveRemote_ && candidates_++ < kMaxCandidates)) {
            return;
        }
        KbRtcApply(id_, kind.c_str(), message.c_str() + split + 1U);
    }

    void Fail(WebRtcCloseReason reason) {
        if (reason_ != WebRtcCloseReason::None) return;
        reason_ = reason;
        state_ = WebRtcState::Closed;
    }

    std::unique_ptr<ISignalingChannel> signaling_;
    WebRtcConfig config_;
    int id_ = 0;
    double startedAt_ = 0.0;
    double lastSend_ = 0.0;
    std::size_t candidates_ = 0U;
    bool haveRemote_ = false;
    WebRtcState state_ = WebRtcState::Connecting;
    WebRtcCloseReason reason_ = WebRtcCloseReason::None;
};

class BrowserSignaling final : public ISignalingChannel {
public:
    ~BrowserSignaling() override { Close(); }

    bool Open(const std::string& url) {
        EmscriptenWebSocketCreateAttributes attributes{};
        attributes.url = url.c_str();
        attributes.protocols = kSignalingWebSocketProtocol;
        attributes.createOnMainThread = false;
        socket_ = KbCreateSignalingSocket(&attributes);
        return socket_ > 0 &&
            emscripten_websocket_set_onopen_callback(socket_, this, OnOpen) == EMSCRIPTEN_RESULT_SUCCESS &&
            emscripten_websocket_set_onmessage_callback(socket_, this, OnMessage) == EMSCRIPTEN_RESULT_SUCCESS &&
            emscripten_websocket_set_onerror_callback(socket_, this, OnError) == EMSCRIPTEN_RESULT_SUCCESS &&
            emscripten_websocket_set_onclose_callback(socket_, this, OnClose) == EMSCRIPTEN_RESULT_SUCCESS;
    }

    bool Send(std::string message) override {
        if (closed_ || message.size() > kMaxSignalingBytes) return false;
        if (!open_) {
            if (pending_.size() >= kMaxQueued) return false;
            pending_.push_back(std::move(message));
            return true;
        }
        return emscripten_websocket_send_utf8_text(socket_, message.c_str()) == EMSCRIPTEN_RESULT_SUCCESS;
    }

    SignalingStatus Receive(std::string& message) override {
        if (!inbox_.empty()) {
            message = std::move(inbox_.front());
            inbox_.pop_front();
            return SignalingStatus::Ok;
        }
        return closed_ ? SignalingStatus::Closed : SignalingStatus::WouldBlock;
    }

    void Close() override {
        closed_ = true;
        if (socket_ <= 0) return;
        static_cast<void>(emscripten_websocket_set_onopen_callback(socket_, nullptr, nullptr));
        static_cast<void>(emscripten_websocket_set_onmessage_callback(socket_, nullptr, nullptr));
        static_cast<void>(emscripten_websocket_set_onerror_callback(socket_, nullptr, nullptr));
        static_cast<void>(emscripten_websocket_set_onclose_callback(socket_, nullptr, nullptr));
        static_cast<void>(emscripten_websocket_close(socket_, 1000U, "signaling done"));
        static_cast<void>(emscripten_websocket_delete(socket_));
        socket_ = 0;
    }

private:
    static bool OnOpen(int, const EmscriptenWebSocketOpenEvent*, void* context) {
        auto& self = *static_cast<BrowserSignaling*>(context);
        self.open_ = true;
        for (const std::string& message : self.pending_) static_cast<void>(emscripten_websocket_send_utf8_text(self.socket_, message.c_str()));
        self.pending_.clear();
        return true;
    }
    static bool OnMessage(int, const EmscriptenWebSocketMessageEvent* event, void* context) {
        auto& self = *static_cast<BrowserSignaling*>(context);
        std::size_t size = event->numBytes;
        if (event->isText && size > 0U && event->data[size - 1U] == 0U) --size;
        if (event->isText && size <= kMaxSignalingBytes && self.inbox_.size() < kMaxQueued)
            self.inbox_.emplace_back(reinterpret_cast<const char*>(event->data), size);
        return true;
    }
    static bool OnError(int, const EmscriptenWebSocketErrorEvent*, void* context) {
        static_cast<BrowserSignaling*>(context)->closed_ = true;
        return true;
    }
    static bool OnClose(int, const EmscriptenWebSocketCloseEvent*, void* context) {
        static_cast<BrowserSignaling*>(context)->closed_ = true;
        return true;
    }

    EMSCRIPTEN_WEBSOCKET_T socket_ = 0;
    std::deque<std::string> inbox_;
    std::deque<std::string> pending_;
    bool open_ = false;
    bool closed_ = false;
};
} // namespace

std::unique_ptr<IWebRtcLink> CreateWebRtcOfferer(std::unique_ptr<ISignalingChannel> signaling, const WebRtcConfig& config, std::string& error) {
    error.clear();
    if (signaling == nullptr || config.maxMessageBytes == 0U || config.timeoutMilliseconds == 0U || config.connectTimeoutMilliseconds == 0U) {
        error = "invalid WebRTC configuration";
        return nullptr;
    }
    auto link = std::make_unique<BrowserLink>(std::move(signaling), config);
    if (!link->Start()) {
        error = "the browser could not create the peer connection";
        return nullptr;
    }
    return link;
}

std::unique_ptr<IWebRtcLink> CreateWebRtcAnswerer(std::unique_ptr<ISignalingChannel>, const WebRtcConfig&, std::string& error) {
    error = "the browser build can only send offers";
    return nullptr;
}

std::unique_ptr<ISignalingChannel> CreateWebSocketSignalingClient(const std::string& url, std::string& error) {
    error.clear();
    if ((!url.starts_with("ws://") && !url.starts_with("wss://")) || url.size() > 2048U ||
        url.find_first_of("\r\n") != std::string::npos || url.find('\0') != std::string::npos ||
        !emscripten_websocket_is_supported()) {
        error = "a supported browser and a ws:// or wss:// signaling endpoint are required";
        return nullptr;
    }
    auto channel = std::make_unique<BrowserSignaling>();
    if (!channel->Open(url)) {
        error = "the browser could not create the signaling WebSocket";
        return nullptr;
    }
    return channel;
}
} // namespace kb::net
#endif
