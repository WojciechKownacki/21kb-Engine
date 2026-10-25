#pragma once

#include "engine/assets/Map2DAsset.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

// Provider-independent access to live ECS tilemaps. The World.Map2D scene system owns the data.
class SceneTilemapRuntime {
public:
    virtual ~SceneTilemapRuntime() = default;
    [[nodiscard]] virtual const kb::assets::Map2DAsset* Map(SceneEntity entity) const = 0;
    [[nodiscard]] virtual bool SetTile(SceneEntity entity, std::uint32_t x, std::uint32_t y,
        std::int32_t floor, const kb::assets::Map2DTile& tile, std::string* error = nullptr) = 0;
    [[nodiscard]] virtual bool PaintGround(SceneEntity entity, std::uint32_t x, std::uint32_t y,
        std::int32_t floor, std::string_view brush, std::string* error = nullptr) = 0;
};

} // namespace kb::scene
