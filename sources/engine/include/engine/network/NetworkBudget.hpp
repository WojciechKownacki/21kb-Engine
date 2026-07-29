#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace kb::network {

struct NetworkBudget { std::uint16_t tickRate = 30U; std::size_t packetBytes = 1200U; std::size_t maxQueuedBytes = 64U * 1024U; };
[[nodiscard]] constexpr bool IsValidNetworkBudget(NetworkBudget budget) noexcept { return budget.tickRate>=10U&&budget.tickRate<=240U&&budget.packetBytes>=64U&&budget.packetBytes<=64U*1024U&&budget.maxQueuedBytes>=budget.packetBytes&&budget.maxQueuedBytes<=16U*1024U*1024U; }
[[nodiscard]] constexpr bool AcceptsPacket(NetworkBudget budget, std::size_t queuedBytes, std::size_t packetBytes) noexcept { return IsValidNetworkBudget(budget)&&packetBytes>0U&&packetBytes<=budget.packetBytes&&queuedBytes<=budget.maxQueuedBytes-packetBytes; }
[[nodiscard]] constexpr std::uint64_t TickDurationMicroseconds(NetworkBudget budget) noexcept { return IsValidNetworkBudget(budget)?1'000'000U/budget.tickRate:0U; }

class NetworkTickClock {
public:
    explicit constexpr NetworkTickClock(NetworkBudget budget) noexcept : budget_(budget) {}

    [[nodiscard]] constexpr std::uint64_t Tick() const noexcept { return tick_; }
    [[nodiscard]] constexpr std::uint32_t RemainderMicroticks() const noexcept { return remainderMicroticks_; }

    [[nodiscard]] constexpr std::uint64_t Advance(std::uint64_t elapsedMicroseconds) noexcept {
        if (!IsValidNetworkBudget(budget_) || tick_ == std::numeric_limits<std::uint64_t>::max()) {
            return 0U;
        }

        constexpr std::uint64_t microsecondsPerSecond = 1'000'000U;
        const std::uint64_t fullSeconds = elapsedMicroseconds / microsecondsPerSecond;
        const std::uint64_t partialMicroseconds = elapsedMicroseconds % microsecondsPerSecond;
        const std::uint64_t capacity = std::numeric_limits<std::uint64_t>::max() - tick_;
        if (fullSeconds > capacity / budget_.tickRate) {
            tick_ = std::numeric_limits<std::uint64_t>::max();
            remainderMicroticks_ = 0U;
            return capacity;
        }

        const std::uint64_t fullSecondTicks = fullSeconds * budget_.tickRate;
        const std::uint64_t scaledPartial = partialMicroseconds * budget_.tickRate + remainderMicroticks_;
        const std::uint64_t partialTicks = scaledPartial / microsecondsPerSecond;
        if (partialTicks >= capacity - fullSecondTicks) {
            tick_ = std::numeric_limits<std::uint64_t>::max();
            remainderMicroticks_ = 0U;
            return capacity;
        }

        const std::uint64_t elapsedTicks = fullSecondTicks + partialTicks;
        tick_ += elapsedTicks;
        remainderMicroticks_ = static_cast<std::uint32_t>(scaledPartial % microsecondsPerSecond);
        return elapsedTicks;
    }

private:
    NetworkBudget budget_{};
    std::uint64_t tick_ = 0U;
    std::uint32_t remainderMicroticks_ = 0U;
};

} // namespace kb::network
