#pragma once

#include <cstdint>

namespace kb::core {
struct RuntimeInspection { std::uint64_t entity=0U; std::uint32_t components=0U; std::uint32_t timers=0U; std::uint32_t subscriptions=0U; std::uint32_t graphFrames=0U; };
class RuntimeInspector final { public: void Publish(RuntimeInspection value) noexcept { latest_=value; } [[nodiscard]] const RuntimeInspection& Snapshot() const noexcept { return latest_; } private: RuntimeInspection latest_{}; };
} // namespace kb::core
