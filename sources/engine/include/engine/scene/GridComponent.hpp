#pragma once

#include <cstdint>
#include <string_view>
#include <cmath>

namespace kb::scene {

struct GridComponent {
    static constexpr std::string_view StableId = "kb21.scene.grid";
    static constexpr std::uint32_t SchemaVersion = 1U;

    float cellSize = 1.0F;
    float floorHeight = 3.0F;
    bool wall = false;
    bool operator==(const GridComponent&) const = default;
};

[[nodiscard]] inline bool IsGridComponentValid(const GridComponent& value) noexcept {
    return std::isfinite(value.cellSize) && value.cellSize > 0.0F && value.cellSize <= 1'000'000.0F &&
        std::isfinite(value.floorHeight) && value.floorHeight >= 0.0F && value.floorHeight <= 1'000'000.0F;
}

} // namespace kb::scene
