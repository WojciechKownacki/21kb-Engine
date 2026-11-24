#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <utility>

namespace kb::net {

// Maps server ticks to local time. The offset is the minimum over the last 2 s of (arrival - tick / rate):
// the minimum ignores datagrams that came late.
class NetworkClock final {
public:
    void SetTickRate(std::uint32_t ticksPerSecond) noexcept {
        const double rate = ticksPerSecond == 0U ? 30.0 : static_cast<double>(ticksPerSecond);
        if (rate != rate_) { rate_ = rate; samples_.clear(); hasLast_ = false; }
    }
    // Call once per received state message; older or repeated ticks are ignored.
    void OnState(std::uint32_t serverTick, double localArrivalSeconds) {
        if (hasLast_ && serverTick <= lastTick_) return;
        hasLast_ = true;
        lastTick_ = serverTick;
        samples_.emplace_back(localArrivalSeconds, localArrivalSeconds - static_cast<double>(serverTick) / rate_);
        while (samples_.front().first < localArrivalSeconds - kWindowSeconds) samples_.pop_front();
        offset_ = samples_.front().second;
        for (const auto& sample : samples_) offset_ = std::min(offset_, sample.second);
    }
    [[nodiscard]] bool Ready() const noexcept { return !samples_.empty(); }
    // Local time at which the server produced `serverTick`.
    [[nodiscard]] double LocalTimeOf(std::uint32_t serverTick) const noexcept { return offset_ + static_cast<double>(serverTick) / rate_; }
    // Mean distance of the samples above the window minimum, seconds.
    [[nodiscard]] double JitterSeconds() const noexcept {
        if (samples_.empty()) return 0.0;
        double sum = 0.0;
        for (const auto& sample : samples_) sum += sample.second - offset_;
        return sum / static_cast<double>(samples_.size());
    }
    // How far in the past remote replicas are shown: clamp(2 tick periods + 2 jitter, 66 ms, 250 ms).
    [[nodiscard]] double ViewDelaySeconds() const noexcept { return std::clamp(2.0 / rate_ + 2.0 * JitterSeconds(), 0.066, 0.25); }

private:
    static constexpr double kWindowSeconds = 2.0;
    double rate_ = 30.0;
    double offset_ = 0.0;
    std::uint32_t lastTick_ = 0U;
    bool hasLast_ = false;
    std::deque<std::pair<double, double>> samples_; // (arrival, arrival - tick / rate)
};

} // namespace kb::net
