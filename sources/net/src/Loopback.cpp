#include "kb/net/Transport.hpp"

#include <algorithm>
#include <utility>

namespace kb::net {

namespace {

constexpr std::size_t kInboxLimit = 1024U;

}

std::string Endpoint::ToString() const {
    return std::to_string((address >> 24U) & 0xFFU) + "." + std::to_string((address >> 16U) & 0xFFU) + "." +
        std::to_string((address >> 8U) & 0xFFU) + "." + std::to_string(address & 0xFFU) + ":" + std::to_string(port);
}

class LoopbackNetwork::Port final : public IDatagramTransport {
public:
    Port(LoopbackNetwork& network, Endpoint local) noexcept : network_(&network), local_(local) {}
    ~Port() override {
        if (network_ != nullptr) {
            network_->Detach(local_);
        }
    }

    [[nodiscard]] TransportStatus Send(const Endpoint& to, std::span<const std::uint8_t> datagram) override {
        return network_->Submit(local_, to, datagram);
    }

    [[nodiscard]] TransportStatus Receive(Endpoint& from, std::span<std::uint8_t> buffer, std::size_t& size) override {
        size = 0U;
        if (inbox_.empty()) {
            return TransportStatus::WouldBlock;
        }
        Packet packet = std::move(inbox_.front());
        inbox_.pop_front();
        from = packet.from;
        size = std::min(packet.data.size(), buffer.size());
        std::copy_n(packet.data.begin(), size, buffer.begin());
        return TransportStatus::Ok;
    }

    [[nodiscard]] Endpoint LocalEndpoint() const override { return local_; }

    // Returns false when the inbox is full, like an overflowing socket buffer.
    [[nodiscard]] bool Push(Packet&& packet) {
        if (inbox_.size() >= kInboxLimit) {
            return false;
        }
        inbox_.push_back(std::move(packet));
        return true;
    }

private:
    LoopbackNetwork* network_;
    Endpoint local_;
    std::deque<Packet> inbox_;
};

LoopbackNetwork::LoopbackNetwork(LinkConditions conditions) : conditions_(conditions) {}

LoopbackNetwork::~LoopbackNetwork() {
    // Transports must be destroyed before the network that created them.
    ports_.clear();
}

std::unique_ptr<IDatagramTransport> LoopbackNetwork::CreateTransport(Endpoint local) {
    if (local.port == 0U || ports_.contains(local)) {
        return nullptr;
    }
    auto port = std::make_unique<Port>(*this, local);
    ports_.emplace(local, port.get());
    return port;
}

void LoopbackNetwork::Detach(const Endpoint& endpoint) noexcept {
    ports_.erase(endpoint);
}

TransportStatus LoopbackNetwork::Submit(const Endpoint& from, const Endpoint& to, std::span<const std::uint8_t> datagram) {
    if (datagram.size() > kMaxDatagramBytes) {
        return TransportStatus::TooLarge;
    }
    const std::uint64_t sequence = counter_++;
    const std::optional<kb::network::NetworkSimulationDecision> decision =
        kb::network::SimulateNetworkPacket(conditions_.simulation, now_, sequence);
    if (!decision.has_value()) {
        return TransportStatus::Error;
    }
    ++stats_.sent;
    if (decision->dropped || decision->disconnected) {
        ++stats_.dropped;
        return TransportStatus::Ok;
    }

    std::uint64_t delay = decision->deliveryDelayMilliseconds;
    if (decision->reordered) {
        // Hold this packet back so that packets sent after it can overtake it.
        delay += 10U + kb::network::NetworkSimulationRandom(conditions_.simulation, now_, sequence ^ 0x51ED270B1A2C3D4EULL) % 40U;
        ++stats_.reordered;
    }
    inFlight_.push_back(Packet{ now_ + delay, sequence * 2U, from, to, std::vector<std::uint8_t>(datagram.begin(), datagram.end()) });

    if (conditions_.duplicatePermille != 0U &&
        kb::network::NetworkSimulationRandom(conditions_.simulation, now_, sequence ^ 0xA5A5A5A5DEADBEEFULL) % 1000U <
            conditions_.duplicatePermille) {
        const std::uint64_t extra = 1U +
            kb::network::NetworkSimulationRandom(conditions_.simulation, now_, sequence ^ 0x0F0F0F0F12345678ULL) % 20U;
        inFlight_.push_back(Packet{ now_ + delay + extra, sequence * 2U + 1U, from, to,
            std::vector<std::uint8_t>(datagram.begin(), datagram.end()) });
        ++stats_.duplicated;
    }
    return TransportStatus::Ok;
}

void LoopbackNetwork::Deliver(Packet&& packet) {
    const auto found = ports_.find(packet.to);
    if (found == ports_.end()) {
        ++stats_.dropped;
        return;
    }
    if (found->second->Push(std::move(packet))) {
        ++stats_.delivered;
    } else {
        ++stats_.inboxOverflow;
    }
}

void LoopbackNetwork::Advance(std::uint64_t nowMilliseconds) {
    now_ = std::max(now_, nowMilliseconds);
    std::vector<Packet> due;
    std::vector<Packet> keep;
    for (Packet& packet : inFlight_) {
        (packet.deliverAt <= now_ ? due : keep).push_back(std::move(packet));
    }
    inFlight_ = std::move(keep);
    std::sort(due.begin(), due.end(), [](const Packet& left, const Packet& right) {
        return left.deliverAt != right.deliverAt ? left.deliverAt < right.deliverAt : left.order < right.order;
    });
    for (Packet& packet : due) {
        Deliver(std::move(packet));
    }
}

void LoopbackNetwork::Inject(const Endpoint& from, const Endpoint& to, std::span<const std::uint8_t> datagram) {
    Deliver(Packet{ now_, counter_++ * 2U, from, to, std::vector<std::uint8_t>(datagram.begin(), datagram.end()) });
}

} // namespace kb::net
