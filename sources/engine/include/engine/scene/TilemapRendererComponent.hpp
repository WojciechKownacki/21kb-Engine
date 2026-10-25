#pragma once

#include <cstdint>
#include <string_view>

namespace kb::scene {

struct TilemapRendererComponent {
    static constexpr std::string_view StableId = "kb21.scene.tilemap-renderer";
    static constexpr std::uint32_t SchemaVersion = 1U;

    std::int32_t sortingOrder = 0;
    bool enabled = true;
    bool operator==(const TilemapRendererComponent&) const = default;
};

} // namespace kb::scene
