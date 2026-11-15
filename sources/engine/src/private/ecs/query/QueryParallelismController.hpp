#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace kb::ecs {

// Decides per execution whether handing a query to the worker pool pays off. It learns from the wall times of
// earlier executions of the same visitor over a similar number of entities: a pool dispatch costs tens of
// microseconds, so a small set or a cheap kernel stays on the calling thread, while a large set or a costly kernel
// keeps the pool. The first run of each arm is a warm-up that the second one replaces (cold caches, sleeping
// workers). The losing arm is retried now and then, rarely when it lost by a wide margin.
// Shared by every query of a plan, so all state is guarded.
class QueryParallelismController {
public:
    struct Choice {
        std::uintptr_t key = 0;
        std::uint32_t bucket = 0;
        bool parallel = false;
        bool timed = false;
    };

    static constexpr std::size_t kMinEntities = 1024U;
    static constexpr double kMinSerialNanoseconds = 40'000.0;

    [[nodiscard]] Choice Choose(std::uintptr_t key, std::size_t entityCount) {
        if (entityCount < kMinEntities) {
            return {};
        }
        Choice choice{ .key = key, .bucket = static_cast<std::uint32_t>(std::bit_width(entityCount)), .parallel = false, .timed = true };
        const std::lock_guard lock{ mutex_ };
        Entry& entry = Find(choice);
        if (entry.serialRuns < 2U || entry.serialNanoseconds < kMinSerialNanoseconds) {
            return choice;
        }
        if (entry.parallelRuns < 2U) {
            choice.parallel = true;
            return choice;
        }
        const bool parallelWins = entry.parallelNanoseconds < entry.serialNanoseconds;
        const double margin = parallelWins ? entry.serialNanoseconds / entry.parallelNanoseconds : entry.parallelNanoseconds / entry.serialNanoseconds;
        const std::uint32_t retryInterval = margin < 2.0 ? 32U : margin < 8.0 ? 256U : 2048U;
        const bool retry = ++entry.runsSinceRetry >= retryInterval;
        if (retry) {
            entry.runsSinceRetry = 0U;
        }
        choice.parallel = parallelWins != retry;
        return choice;
    }

    void Record(const Choice& choice, std::uint64_t nanoseconds) {
        const std::lock_guard lock{ mutex_ };
        Entry& entry = Find(choice);
        double& average = choice.parallel ? entry.parallelNanoseconds : entry.serialNanoseconds;
        std::uint32_t& runs = choice.parallel ? entry.parallelRuns : entry.serialRuns;
        average = runs < 2U ? static_cast<double>(nanoseconds) : (average * 0.7) + (static_cast<double>(nanoseconds) * 0.3);
        ++runs;
    }

private:
    struct Entry {
        std::uintptr_t key = 0;
        std::uint32_t bucket = 0;
        bool used = false;
        double serialNanoseconds = 0.0;
        double parallelNanoseconds = 0.0;
        std::uint32_t serialRuns = 0;
        std::uint32_t parallelRuns = 0;
        std::uint32_t runsSinceRetry = 0;
    };

    [[nodiscard]] Entry& Find(const Choice& choice) noexcept {
        for (Entry& entry : entries_) {
            if (entry.used && entry.key == choice.key && entry.bucket == choice.bucket) {
                return entry;
            }
        }
        Entry& entry = entries_[next_];
        next_ = (next_ + 1U) % entries_.size();
        entry = Entry{};
        entry.key = choice.key;
        entry.bucket = choice.bucket;
        entry.used = true;
        return entry;
    }

    std::mutex mutex_;
    std::array<Entry, 8> entries_{};
    std::size_t next_ = 0;
};

} // namespace kb::ecs
