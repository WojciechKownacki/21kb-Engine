#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace kb::core {

struct ReadSnapshot {
    std::uint64_t revision = 0U;
};

// A reader receives an immutable retained value. Publishing atomically replaces
// the retained pointer, so readers never block the producer or touch
// scene-owned mutable storage.
template <typename Snapshot>
class ReadSnapshotPublisher final {
public:
    ReadSnapshotPublisher()
        : latest_(std::make_shared<const Snapshot>()) {}

    void Publish(Snapshot snapshot) {
        latest_.store(std::make_shared<const Snapshot>(std::move(snapshot)),
            std::memory_order_release);
    }

    [[nodiscard]] std::shared_ptr<const Snapshot> Read() const {
        return latest_.load(std::memory_order_acquire);
    }

private:
    std::atomic<std::shared_ptr<const Snapshot>> latest_;
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
