#pragma once
#include <array>
#include <string>
#include <vector>
#include "engine/assets/Map2DAsset.hpp"
#include "engine/assets/AssetId.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/SceneEntity.hpp"
namespace kb::scene { class Scene; }
namespace kb::world2d {
// One convex piece of a map's collision in the plane of the map, for the 2D physics: x to the right and the second coordinate up
// (Y for a wall map, Z for a flat one), relative to the map object, in the units of the world.
struct Map2DShape2D {
    std::vector<std::array<float, 2>> points;
    bool oneWay = false;
    bool trigger = false;
    float friction = 0.5F;
    float restitution = 0.0F;
};
// The shapes of the ground floor of a map; neighbouring cells with the same shape are joined.
[[nodiscard]] bool BuildMap2DShapes2D(const kb::assets::Map2DAsset& map, std::vector<Map2DShape2D>& output, std::string* error);
[[nodiscard]] bool BuildMap2DCollision(kb::scene::Scene& scene, kb::scene::SceneEntity owner,
    const kb::assets::Map2DAsset& map, std::vector<kb::scene::ColliderComponent>& output,
    std::vector<kb::assets::AssetId>* runtimeAssets, std::string* error);
}
