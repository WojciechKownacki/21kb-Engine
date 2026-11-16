#include "kb/net/WebRtcTransport.hpp"

#include <algorithm>
#include <chrono>
#include <map>

namespace kb::net {
namespace {

using Clock = std::chrono::steady_clock;

TransportStatus SendDatagram(IWebRtcLink& link, std::span<const std::uint8_t> datagram) {
    if (datagram.size() > kMaxDatagramBytes) return TransportStatus::TooLarge;
    return link.Send(WebRtcChannel::Unreliable, datagram);
}

bool TakeDatagram(IWebRtcLink& link, std::span<std::uint8_t> buffer, std::size_t& size) {
    std::vector<std::uint8_t> message;
    if (!link.Receive(WebRtcChannel::Unreliable, message)) return false;
    size = std::min(buffer.size(), message.size());
    std::copy_n(message.begin(), size, buffer.begin());
    return true;
}

class WebRtcClientTransport final : public IDatagramTransport {
public:
    WebRtcClientTransport(std::unique_ptr<IWebRtcLink> link, Endpoint peer) : link_(std::move(link)), peer_(peer) {}

    TransportStatus Send(const Endpoint& to, std::span<const std::uint8_t> datagram) override {
        link_->Update();
        return to == peer_ ? SendDatagram(*link_, datagram) : TransportStatus::Error;
    }

    TransportStatus Receive(Endpoint& from, std::span<std::uint8_t> buffer, std::size_t& size) override {
        link_->Update();
        size = 0U;
        if (TakeDatagram(*link_, buffer, size)) {
            from = peer_;
            return TransportStatus::Ok;
        }
        return link_->State() == WebRtcState::Closed ? TransportStatus::Error : TransportStatus::WouldBlock;
    }

    Endpoint LocalEndpoint() const override { return Endpoint::Loopback(0U); }

private:
    std::unique_ptr<IWebRtcLink> link_;
    Endpoint peer_;
};

class WebRtcServerTransport final : public IDatagramTransport {
public:
    WebRtcServerTransport(WebRtcServerOptions options, std::shared_ptr<SignalingListener> signaling, std::unique_ptr<IDatagramTransport> inner)
        : options_(std::move(options)), signaling_(std::move(signaling)), inner_(std::move(inner)) {}

    TransportStatus Send(const Endpoint& to, std::span<const std::uint8_t> datagram) override {
        const auto found = peers_.find(to);
        if (found != peers_.end()) return SendDatagram(*found->second, datagram);
        return inner_ != nullptr ? inner_->Send(to, datagram) : TransportStatus::Error;
    }

    TransportStatus Receive(Endpoint& from, std::span<std::uint8_t> buffer, std::size_t& size) override {
        Maintain();
        preferRtc_ = !preferRtc_;
        if (preferRtc_ && ReceiveRtc(from, buffer, size)) return TransportStatus::Ok;
        TransportStatus status = TransportStatus::WouldBlock;
        if (inner_ != nullptr) {
            status = inner_->Receive(from, buffer, size);
            if (status == TransportStatus::Ok) return status;
        }
        if (ReceiveRtc(from, buffer, size)) return TransportStatus::Ok;
        size = 0U;
        return status == TransportStatus::Error ? status : TransportStatus::WouldBlock;
    }

    Endpoint LocalEndpoint() const override { return inner_ != nullptr ? inner_->LocalEndpoint() : Endpoint::Loopback(0U); }

private:
    static Endpoint Identity(std::uint64_t id) {
        return { static_cast<std::uint32_t>(id >> 16U), static_cast<std::uint16_t>(id & 0xFFFFU) };
    }

    // Accepts new peers, polls every link and forgets the closed ones.
    void Maintain() {
        const auto now = Clock::now();
        if (now - lastMaintain_ < std::chrono::milliseconds(2)) return;
        lastMaintain_ = now;
        while (auto channel = signaling_->Pop()) {
            std::string error;
            if (peers_.size() >= options_.maxClients || nextId_ >= (1ULL << 40U)) continue;
            auto link = CreateWebRtcAnswerer(std::move(channel), options_.link, error);
            if (link != nullptr) peers_.emplace(Identity(nextId_++), std::move(link));
        }
        for (auto& peer : peers_) peer.second->Update();
        std::erase_if(peers_, [](const auto& peer) { return peer.second->State() == WebRtcState::Closed; });
    }

    bool ReceiveRtc(Endpoint& from, std::span<std::uint8_t> buffer, std::size_t& size) {
        auto it = peers_.upper_bound(cursor_);
        for (std::size_t visited = 0U; visited < peers_.size(); ++visited, ++it) {
            if (it == peers_.end()) it = peers_.begin();
            if (TakeDatagram(*it->second, buffer, size)) {
                from = cursor_ = it->first;
                return true;
            }
        }
        return false;
    }

    WebRtcServerOptions options_;
    std::shared_ptr<SignalingListener> signaling_;
    std::unique_ptr<IDatagramTransport> inner_;
    std::map<Endpoint, std::unique_ptr<IWebRtcLink>> peers_;
    Endpoint cursor_{};
    Clock::time_point lastMaintain_{};
    std::uint64_t nextId_ = kWebRtcIdentityBase;
    bool preferRtc_ = false;
};
} // namespace

std::unique_ptr<IDatagramTransport> CreateWebRtcClientTransport(
    std::unique_ptr<ISignalingChannel> signaling, const WebRtcConfig& config, Endpoint peer, std::string& error) {
    auto link = CreateWebRtcOfferer(std::move(signaling), config, error);
    if (link == nullptr) return nullptr;
    return std::make_unique<WebRtcClientTransport>(std::move(link), peer);
}

std::unique_ptr<IDatagramTransport> CreateWebRtcServerTransport(const WebRtcServerOptions& options,
    std::shared_ptr<SignalingListener> signaling, std::unique_ptr<IDatagramTransport> inner, std::string& error) {
    error.clear();
    if (signaling == nullptr || options.maxClients == 0U || options.maxClients > 4096U) {
        error = "invalid WebRTC listener limits";
        return nullptr;
    }
    return std::make_unique<WebRtcServerTransport>(options, std::move(signaling), std::move(inner));
}
} // namespace kb::net
