// Native WebRTC links and the WebSocket signaling client, on a bundled WebRTC library.

#include "kb/net/WebRtcTransport.hpp"

#include <rtc/rtc.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>

namespace kb::net {
namespace {

using Clock = std::chrono::steady_clock;

std::int64_t NowMilliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}

constexpr std::size_t kMaxSignalingBytes = 64U * 1024U;
constexpr std::size_t kMaxCandidates = 64U;
constexpr std::size_t kMaxInboxMessages = 4096U;
constexpr std::size_t kMaxInboxBytes = 16U * 1024U * 1024U;
constexpr std::size_t kReliableBufferBytes = 8U * 1024U * 1024U;
constexpr std::size_t kUnreliableBufferBytes = 256U * 1024U;
constexpr const char* kLabels[2] = { "reliable", "unreliable" };

class NativeLink final : public IWebRtcLink {
public:
    NativeLink(std::unique_ptr<ISignalingChannel> signaling, WebRtcConfig config, bool offerer)
        : signaling_(std::move(signaling)), config_(std::move(config)), offerer_(offerer) {}

    ~NativeLink() override { Close(); }

    bool Start(std::string& error) {
        try {
            rtc::Configuration configuration;
            for (const std::string& server : config_.iceServers) configuration.iceServers.emplace_back(server);
            if (!config_.bindAddress.empty()) configuration.bindAddress = config_.bindAddress;
            configuration.maxMessageSize = config_.maxMessageBytes;
            connection_ = std::make_shared<rtc::PeerConnection>(configuration);
            connection_->onStateChange([this](rtc::PeerConnection::State state) {
                if (state == rtc::PeerConnection::State::Failed) Fail(WebRtcCloseReason::Error);
                else if (state == rtc::PeerConnection::State::Disconnected || state == rtc::PeerConnection::State::Closed)
                    Fail(WebRtcCloseReason::Remote);
            });
            connection_->onLocalDescription([this](rtc::Description description) {
                static_cast<void>(signaling_->Send(description.typeString() + "\n" + std::string(description)));
            });
            connection_->onLocalCandidate([this](rtc::Candidate candidate) {
                static_cast<void>(signaling_->Send("candidate\n" + candidate.mid() + "\n" + candidate.candidate()));
            });
            if (offerer_) {
                rtc::DataChannelInit unreliable;
                unreliable.reliability.unordered = true;
                unreliable.reliability.maxRetransmits = 0U;
                Attach(connection_->createDataChannel(kLabels[0]), 0U);
                Attach(connection_->createDataChannel(kLabels[1], unreliable), 1U);
            } else {
                connection_->onDataChannel([this](std::shared_ptr<rtc::DataChannel> channel) {
                    const std::string label = channel->label();
                    for (std::size_t index = 0U; index < 2U; ++index) {
                        if (label == kLabels[index]) {
                            Attach(std::move(channel), index);
                            return;
                        }
                    }
                });
            }
        } catch (const std::exception& exception) {
            error = exception.what();
            return false;
        }
        startedAt_ = NowMilliseconds();
        lastReceive_ = lastSend_ = startedAt_;
        return true;
    }

    void Update() override {
        if (State() == WebRtcState::Closed) return;
        std::string message;
        while (State() != WebRtcState::Closed && signaling_->Receive(message) == SignalingStatus::Ok) Apply(message);
        const std::int64_t now = NowMilliseconds();
        if (State() == WebRtcState::Connecting) {
            if (Opened(0U) && Opened(1U)) {
                lastReceive_ = lastSend_ = now;
                state_ = WebRtcState::Open;
                signaling_->Close(); // the peers are connected; nothing more needs to be negotiated
            } else if (now - startedAt_ > static_cast<std::int64_t>(config_.connectTimeoutMilliseconds)) {
                Fail(WebRtcCloseReason::ConnectTimeout);
            }
            return;
        }
        if (now - lastReceive_ > static_cast<std::int64_t>(config_.timeoutMilliseconds)) {
            Fail(WebRtcCloseReason::Timeout);
        } else if (now - lastSend_ > static_cast<std::int64_t>(config_.timeoutMilliseconds / 4U)) {
            SendRaw(1U, {});
        }
    }

    WebRtcState State() const override { return state_.load(); }
    WebRtcCloseReason CloseReason() const override { return reason_.load(); }

    TransportStatus Send(WebRtcChannel channel, std::span<const std::uint8_t> message) override {
        if (State() == WebRtcState::Closed) return TransportStatus::Error;
        if (State() == WebRtcState::Connecting) return TransportStatus::WouldBlock;
        const std::size_t index = channel == WebRtcChannel::Reliable ? 0U : 1U;
        if (message.empty() || message.size() > config_.maxMessageBytes) return TransportStatus::TooLarge;
        const std::size_t limit = index == 0U ? kReliableBufferBytes : kUnreliableBufferBytes;
        try {
            if (channels_[index]->bufferedAmount() > limit) return TransportStatus::WouldBlock;
            if (message.size() > channels_[index]->maxMessageSize()) return TransportStatus::TooLarge;
        } catch (const std::exception&) {
            return TransportStatus::Error;
        }
        return SendRaw(index, message) ? TransportStatus::Ok : TransportStatus::Error;
    }

    bool Receive(WebRtcChannel channel, std::vector<std::uint8_t>& message) override {
        std::lock_guard lock(mutex_);
        auto& inbox = inbox_[channel == WebRtcChannel::Reliable ? 0U : 1U];
        if (inbox.empty()) return false;
        message = std::move(inbox.front());
        inbox.pop_front();
        inboxBytes_ -= message.size();
        return true;
    }

    void Close() override {
        Fail(WebRtcCloseReason::Local);
        signaling_->Close();
        if (connection_ == nullptr) return;
        // Callbacks capture `this`, so none may run once the link is gone.
        connection_->resetCallbacks();
        for (auto& channel : channels_) {
            if (channel != nullptr) channel->resetCallbacks();
        }
        try { connection_->close(); } catch (const std::exception&) {}
        connection_.reset();
        channels_[0].reset();
        channels_[1].reset();
    }

private:
    void Attach(std::shared_ptr<rtc::DataChannel> channel, std::size_t index) {
        {
            std::lock_guard lock(mutex_);
            if (channels_[index] != nullptr) return; // a peer that opens a label twice gets the first channel only
            channels_[index] = channel;
        }
        channel->onMessage([this, index](rtc::binary message) { Deliver(index, std::move(message)); }, [](std::string) {});
        channel->onClosed([this] { Fail(WebRtcCloseReason::Remote); });
    }

    // The library is never called with the mutex held: its own locks are taken around our callbacks.
    [[nodiscard]] bool Opened(std::size_t index) {
        std::shared_ptr<rtc::DataChannel> channel;
        {
            std::lock_guard lock(mutex_);
            channel = channels_[index];
        }
        return channel != nullptr && channel->isOpen();
    }

    bool SendRaw(std::size_t index, std::span<const std::uint8_t> message) {
        try {
            static_cast<void>(channels_[index]->send(reinterpret_cast<const rtc::byte*>(message.data()), message.size()));
        } catch (const std::exception&) {
            return false;
        }
        lastSend_ = NowMilliseconds();
        return true;
    }

    void Deliver(std::size_t index, rtc::binary message) {
        lastReceive_ = NowMilliseconds();
        if (message.empty()) return;
        std::lock_guard lock(mutex_);
        if (inbox_[index].size() >= kMaxInboxMessages || inboxBytes_ + message.size() > kMaxInboxBytes) {
            if (index == 0U) Fail(WebRtcCloseReason::Error); // a reliable message cannot be dropped
            return;
        }
        inboxBytes_ += message.size();
        inbox_[index].emplace_back(reinterpret_cast<const std::uint8_t*>(message.data()),
            reinterpret_cast<const std::uint8_t*>(message.data()) + message.size());
    }

    void Apply(const std::string& message) {
        if (message.size() > kMaxSignalingBytes) return;
        const std::size_t split = message.find('\n');
        if (split == std::string::npos) return;
        const std::string kind = message.substr(0U, split);
        const std::string body = message.substr(split + 1U);
        try {
            if (kind == (offerer_ ? "answer" : "offer")) {
                if (haveRemote_) return;
                haveRemote_ = true;
                connection_->setRemoteDescription(rtc::Description(body, kind));
            } else if (kind == "candidate" && haveRemote_ && candidates_ < kMaxCandidates) {
                const std::size_t line = body.find('\n');
                if (line == std::string::npos) return;
                ++candidates_;
                connection_->addRemoteCandidate(rtc::Candidate(body.substr(line + 1U), body.substr(0U, line)));
            }
        } catch (const std::exception&) {
            Fail(WebRtcCloseReason::Error);
        }
    }

    void Fail(WebRtcCloseReason reason) {
        auto expected = WebRtcCloseReason::None;
        if (reason_.compare_exchange_strong(expected, reason)) state_ = WebRtcState::Closed;
    }

    std::unique_ptr<ISignalingChannel> signaling_;
    WebRtcConfig config_;
    bool offerer_;
    std::shared_ptr<rtc::PeerConnection> connection_;
    std::shared_ptr<rtc::DataChannel> channels_[2];
    std::mutex mutex_;
    std::deque<std::vector<std::uint8_t>> inbox_[2];
    std::size_t inboxBytes_ = 0U;
    std::atomic<WebRtcState> state_{ WebRtcState::Connecting };
    std::atomic<WebRtcCloseReason> reason_{ WebRtcCloseReason::None };
    std::atomic<std::int64_t> lastReceive_{ 0 };
    std::atomic<std::int64_t> lastSend_{ 0 };
    std::int64_t startedAt_ = 0;
    std::size_t candidates_ = 0U;
    bool haveRemote_ = false;
};

std::unique_ptr<IWebRtcLink> MakeLink(std::unique_ptr<ISignalingChannel> signaling, const WebRtcConfig& config, bool offerer, std::string& error) {
    error.clear();
    if (signaling == nullptr || config.maxMessageBytes == 0U || config.maxMessageBytes > 1U << 30U ||
        config.timeoutMilliseconds == 0U || config.connectTimeoutMilliseconds == 0U) {
        error = "invalid WebRTC configuration";
        return nullptr;
    }
    auto link = std::make_unique<NativeLink>(std::move(signaling), config, offerer);
    if (!link->Start(error)) return nullptr;
    return link;
}

class WebSocketSignaling final : public ISignalingChannel {
public:
    ~WebSocketSignaling() override { Close(); }

    bool Open(const std::string& url) {
        rtc::WebSocketConfiguration configuration;
        configuration.protocols = { "21kb.signal.v1" };
        configuration.connectionTimeout = std::chrono::seconds(10);
        socket_ = std::make_shared<rtc::WebSocket>(configuration);
        socket_->onOpen([this] {
            std::lock_guard lock(mutex_);
            open_ = true;
            for (std::string& message : pending_) SendNow(message);
            pending_.clear();
        });
        socket_->onClosed([this] { closed_ = true; });
        socket_->onError([this](std::string) { closed_ = true; });
        socket_->onMessage([](rtc::binary) {}, [this](std::string message) {
            std::lock_guard lock(mutex_);
            if (inbox_.size() < kMaxQueued && message.size() <= kMaxSignalingBytes) inbox_.push_back(std::move(message));
        });
        try { socket_->open(url); } catch (const std::exception&) { return false; }
        return true;
    }

    bool Send(std::string message) override {
        if (closed_ || message.size() > kMaxSignalingBytes) return false;
        std::lock_guard lock(mutex_);
        if (open_) return SendNow(message);
        if (pending_.size() >= kMaxQueued) return false;
        pending_.push_back(std::move(message));
        return true;
    }

    SignalingStatus Receive(std::string& message) override {
        std::lock_guard lock(mutex_);
        if (!inbox_.empty()) {
            message = std::move(inbox_.front());
            inbox_.pop_front();
            return SignalingStatus::Ok;
        }
        return closed_ ? SignalingStatus::Closed : SignalingStatus::WouldBlock;
    }

    void Close() override {
        closed_ = true;
        if (socket_ == nullptr) return;
        socket_->resetCallbacks();
        try { socket_->close(); } catch (const std::exception&) {}
        socket_.reset();
    }

private:
    static constexpr std::size_t kMaxQueued = 64U;

    bool SendNow(const std::string& message) {
        try { socket_->send(message); return true; } catch (const std::exception&) { return false; }
    }

    std::shared_ptr<rtc::WebSocket> socket_;
    std::mutex mutex_;
    std::deque<std::string> inbox_;
    std::deque<std::string> pending_;
    std::atomic<bool> closed_{ false };
    bool open_ = false;
};
} // namespace

std::unique_ptr<IWebRtcLink> CreateWebRtcOfferer(std::unique_ptr<ISignalingChannel> signaling, const WebRtcConfig& config, std::string& error) {
    return MakeLink(std::move(signaling), config, true, error);
}

std::unique_ptr<IWebRtcLink> CreateWebRtcAnswerer(std::unique_ptr<ISignalingChannel> signaling, const WebRtcConfig& config, std::string& error) {
    return MakeLink(std::move(signaling), config, false, error);
}

std::unique_ptr<ISignalingChannel> CreateWebSocketSignalingClient(const std::string& url, std::string& error) {
    error.clear();
    if ((!url.starts_with("ws://") && !url.starts_with("wss://")) || url.size() > 2048U ||
        url.find_first_of("\r\n") != std::string::npos) {
        error = "a ws:// or wss:// signaling endpoint is required";
        return nullptr;
    }
    auto channel = std::make_unique<WebSocketSignaling>();
    if (!channel->Open(url)) {
        error = "the signaling WebSocket could not be opened";
        return nullptr;
    }
    return channel;
}
} // namespace kb::net
