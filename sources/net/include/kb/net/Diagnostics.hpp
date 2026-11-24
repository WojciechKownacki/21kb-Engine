#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <utility>

namespace kb::net {

enum class DiagCode : std::uint16_t {
    EntityLimit, OversizeRecord, SendQueueFull, HandshakeVersion, HandshakeContent,
    HandshakeRateLimited, HandshakeServerFull, RpcRejected, InterestBudget,
    ObjectCountWarning, VersionUnchecked, VisibilityUnbounded, RateLimited, ComponentGroupLimit, NotAuthority,
};

struct DiagEntry {
    DiagCode code{};
    std::uint64_t count = 0U;
    std::uint64_t lastMs = 0U;
    char detail[160]{};
};

[[nodiscard]] std::string_view DiagName(DiagCode code) noexcept;

class DiagLog final {
public:
    void Report(DiagCode code, std::string_view detail) noexcept;
    [[nodiscard]] std::span<const DiagEntry> Entries() const noexcept { return { entries_.data(), size_ }; }
    [[nodiscard]] std::uint64_t Total() const noexcept { return total_; }
    void SetSink(std::function<void(std::string_view)> sink) { sink_ = std::move(sink); }

private:
    std::array<DiagEntry, 32U> entries_{};
    std::array<std::uint64_t, 32U> emittedAt_{};
    std::array<bool, 32U> emitted_{};
    std::size_t size_ = 0U;
    std::uint64_t total_ = 0U;
    std::function<void(std::string_view)> sink_;
};

} // namespace kb::net
