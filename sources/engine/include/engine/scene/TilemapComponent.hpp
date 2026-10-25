#pragma once

#include <cstdint>
#include <string_view>

namespace kb::scene {

struct TilemapComponent {
    static constexpr std::string_view StableId = "kb21.scene.tilemap";
    static constexpr std::uint32_t SchemaVersion = 1U;

    std::uint64_t mapAssetId = 0U;
    bool locked = false;
    bool operator==(const TilemapComponent&) const = default;
};

} // namespace kb::scene
