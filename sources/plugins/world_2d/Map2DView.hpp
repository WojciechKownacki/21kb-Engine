#pragma once

#include "engine/assets/Map2DAsset.hpp"
#include "engine/assets/AssetId.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <filesystem>
#include <string>

namespace kb::scene { class Scene; }

namespace kb::world2d {

inline constexpr std::string_view kMap2DCameraName = "Map Camera";
inline constexpr std::string_view kMap2DCollisionName = "Map Collision";
inline constexpr std::string_view kMap2DProfilePath = "/Game/Maps/PP_Map2D.kbppfx";

struct Map2DViewResult {
    kb::scene::SceneEntity camera{};
    bool cameraCreated = false;
    bool profileAssigned = false;
};

// Attach authored ECS data while retaining the existing mesh backend and asset identifiers.
// Reapplying preserves layer visibility, sorting and collision preferences.
void SetUpMap2DComponents(kb::scene::Scene& scene, kb::scene::SceneEntity entity,
    std::uint64_t mapAssetId, const kb::assets::Map2DAsset& map);

// Rebuilds static collision boxes under the map, merging adjacent solid tiles. These ordinary
// scene colliders are saved and packaged with the scene; custom children of the map are untouched.
[[nodiscard]] bool SetUpMap2DCollision(
    kb::scene::Scene& scene, kb::scene::SceneEntity mapEntity, const kb::assets::Map2DAsset& map,
    bool* changed = nullptr, std::string* error = nullptr, std::vector<kb::assets::AssetId>* runtimeAssets = nullptr);

// How a player sees the map, as Remere's client box shows it: an orthographic "Map Camera" under
// the map entity framing viewHeight tiles vertically at the map's centre (straight down on a flat
// map, straight at a wall); horizontal coverage follows the viewport aspect ratio. This becomes
// the primary camera for Play. A scene with no post-process profile also
// gets /Game/Maps/PP_Map2D.kbppfx - no tonemapping, exposure or screen effects - so Play shows the
// sprites with the colours the Map Editor shows. Run again it updates the same camera.
[[nodiscard]] bool SetUpMap2DView(
    kb::scene::Scene& scene,
    kb::scene::SceneEntity mapEntity,
    const kb::assets::Map2DAsset& map,
    const std::filesystem::path& assetsRoot,
    Map2DViewResult* result = nullptr,
    std::string* error = nullptr);

// A scene with no post-process profile gets /Game/Maps/PP_Map2D.kbppfx (no tonemapping, exposure or screen effects), so unlit
// sprites show their own colours instead of being brightened by automatic exposure. True when the profile was assigned; false
// when the scene already has a profile or the profile could not be written. Sprite 2D scenes use it too.
bool UseExactColors(kb::scene::Scene& scene, const std::filesystem::path& assetsRoot);

} // namespace kb::world2d
