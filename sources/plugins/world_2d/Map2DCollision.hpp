#pragma once
#include "engine/assets/Map2DAsset.hpp"
#include "engine/assets/AssetId.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/SceneEntity.hpp"
namespace kb::scene { class Scene; }
namespace kb::world2d {
[[nodiscard]] bool BuildMap2DCollision(kb::scene::Scene& scene, kb::scene::SceneEntity owner,
    const kb::assets::Map2DAsset& map, std::vector<kb::scene::ColliderComponent>& output,
    std::vector<kb::assets::AssetId>* runtimeAssets, std::string* error);
}
