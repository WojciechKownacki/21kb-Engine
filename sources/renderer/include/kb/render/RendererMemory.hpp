#pragma once

#include <cstdint>
#include <optional>

namespace kb::render {
class Renderer;

// Driver-reported process usage on the active adapter (local + shared memory).
// D3D11 only; unavailable before initialization, after shutdown or without driver support.
[[nodiscard]] std::optional<std::uint64_t> ProcessGpuMemoryBytes(const Renderer& renderer) noexcept;

} // namespace kb::render
