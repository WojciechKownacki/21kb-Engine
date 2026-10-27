#pragma once

#include <cstdint>
#include <string_view>

namespace kb::scene {

struct TilemapColliderComponent {
    static constexpr std::string_view StableId = "kb21.scene.tilemap-collider";
    static constexpr std::uint32_t SchemaVersion = 1U;

    bool enabled = true;
    bool operator==(const TilemapColliderComponent&) const = default;
};

} // namespace kb::scene
