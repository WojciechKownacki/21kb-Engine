#include "kb/net/Signaling.hpp"

namespace kb::net {
namespace {

constexpr std::size_t kMaxQueued = 256U;

struct MemoryLink {
    std::mutex mutex;
    std::deque<std::string> inbox[2];
    bool closed[2]{};
};

class MemorySignaling final : public ISignalingChannel {
public:
    MemorySignaling(std::shared_ptr<MemoryLink> link, std::size_t side) : link_(std::move(link)), side_(side) {}
    ~MemorySignaling() override { Close(); }

    bool Send(std::string message) override {
        std::lock_guard lock(link_->mutex);
        auto& peerInbox = link_->inbox[1U - side_];
        if (link_->closed[0] || link_->closed[1] || peerInbox.size() >= kMaxQueued) return false;
        peerInbox.push_back(std::move(message));
        return true;
    }

    SignalingStatus Receive(std::string& message) override {
        std::lock_guard lock(link_->mutex);
        auto& inbox = link_->inbox[side_];
        if (!inbox.empty()) {
            message = std::move(inbox.front());
            inbox.pop_front();
            return SignalingStatus::Ok;
        }
        return link_->closed[0] || link_->closed[1] ? SignalingStatus::Closed : SignalingStatus::WouldBlock;
    }

    void Close() override {
        std::lock_guard lock(link_->mutex);
        link_->closed[side_] = true;
    }

private:
    std::shared_ptr<MemoryLink> link_;
    std::size_t side_;
};
} // namespace

std::pair<std::unique_ptr<ISignalingChannel>, std::unique_ptr<ISignalingChannel>> CreateSignalingPair() {
    auto link = std::make_shared<MemoryLink>();
    return { std::make_unique<MemorySignaling>(link, 0U), std::make_unique<MemorySignaling>(link, 1U) };
}
} // namespace kb::net
