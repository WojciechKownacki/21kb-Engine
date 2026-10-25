#pragma once

#include <chrono>
#include <cstdint>

namespace kb::game {

class WebDownloadProgress {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr std::chrono::seconds IdleTimeout{30};

    void Start(Clock::time_point now) noexcept {
        bytes_ = 0;
        lastProgress_ = now;
    }

    void Observe(std::uint64_t bytes, Clock::time_point now) noexcept {
        if (bytes > bytes_) {
            bytes_ = bytes;
            lastProgress_ = now;
        }
    }

    [[nodiscard]] std::chrono::milliseconds Remaining(Clock::time_point now) const noexcept {
        const auto deadline = lastProgress_ + IdleTimeout;
        return now >= deadline ? std::chrono::milliseconds{0}
            : std::chrono::ceil<std::chrono::milliseconds>(deadline - now);
    }

private:
    std::uint64_t bytes_ = 0;
    Clock::time_point lastProgress_{};
};

} // namespace kb::game
