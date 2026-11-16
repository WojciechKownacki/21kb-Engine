#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kb::core {

// An opt-in record of when each step of a launch finished, in milliseconds from the start of the process. Nothing is
// recorded until Enable, so a mark left in the launch path costs one atomic load. Marks may come from any thread.
class StartupTrace final {
public:
    struct Mark {
        std::string step;
        double milliseconds = 0.0;
    };

    // Starts recording; `sinceProcessStartMilliseconds` is how old the process already is.
    static void Enable(double sinceProcessStartMilliseconds) {
        State& state = Instance();
        const std::scoped_lock lock{ state.mutex };
        state.origin = std::chrono::steady_clock::now() -
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double, std::milli>(sinceProcessStartMilliseconds));
        state.enabled.store(true, std::memory_order_release);
    }

    [[nodiscard]] static bool Enabled() noexcept {
        return Instance().enabled.load(std::memory_order_acquire);
    }

    static void Record(std::string_view step) {
        State& state = Instance();
        if (!state.enabled.load(std::memory_order_acquire)) {
            return;
        }
        const std::scoped_lock lock{ state.mutex };
        state.marks.push_back(Mark{ std::string{ step },
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - state.origin).count() });
    }

    [[nodiscard]] static std::vector<Mark> Marks() {
        State& state = Instance();
        const std::scoped_lock lock{ state.mutex };
        return state.marks;
    }

private:
    struct State {
        std::atomic<bool> enabled{ false };
        std::mutex mutex;
        std::chrono::steady_clock::time_point origin{};
        std::vector<Mark> marks;
    };

    [[nodiscard]] static State& Instance() noexcept {
        static State state;
        return state;
    }
};

} // namespace kb::core
