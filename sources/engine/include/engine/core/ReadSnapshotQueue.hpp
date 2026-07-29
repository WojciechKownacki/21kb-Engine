#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace kb::core {

struct ReadSnapshot {
    std::uint64_t revision = 0U;
};

// A reader receives an immutable retained value. Publishing replaces the
// retained pointer under a short lock, so a worker can keep an older snapshot
// while the main thread builds and publishes the next one without touching
// scene-owned mutable storage.
template <typename Snapshot>
class ReadSnapshotPublisher final {
public:
    ReadSnapshotPublisher()
        : latest_(std::make_shared<const Snapshot>()) {}

    void Publish(Snapshot snapshot) {
        auto published = std::make_shared<const Snapshot>(std::move(snapshot));
        std::lock_guard lock{mutex_};
        latest_ = std::move(published);
    }

    [[nodiscard]] std::shared_ptr<const Snapshot> Read() const {
        std::lock_guard lock{mutex_};
        return latest_;
    }

private:
    mutable std::mutex mutex_;
    std::shared_ptr<const Snapshot> latest_;
};

struct RuntimeCommand {
    std::uint64_t target = 0U;
    std::uint32_t kind = 0U;
};

// Multi-producer, single-consumer FIFO. Drain swaps ownership of one complete
// batch, so the consumer never iterates storage a producer can reallocate.
template <typename Command = RuntimeCommand>
class CommandQueue final {
public:
    void Enqueue(Command command) {
        std::lock_guard lock{mutex_};
        pending_.push_back(std::move(command));
    }

    [[nodiscard]] std::vector<Command> Drain() {
        std::vector<Command> result;
        std::lock_guard lock{mutex_};
        result.swap(pending_);
        return result;
    }

private:
    std::mutex mutex_;
    std::vector<Command> pending_;
};

} // namespace kb::core
