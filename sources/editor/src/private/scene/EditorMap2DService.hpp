#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/assets/Map2DAsset.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace kb::scene { class Scene; }
namespace kb::assets { struct AssetMetadata; }

namespace kb::editor {

// Authoring bridge for the ECS Grid/Tilemap components, their shared source asset and render preview.
class EditorMap2DService final {
public:
    EditorMap2DService() = delete;
    [[nodiscard]] static bool RefreshPreview(kb::scene::Scene& scene, kb::assets::AssetId assetId,
        const kb::assets::Map2DAsset& map, double seconds = 0, std::string* error = nullptr);
    static void ClearPreviews(kb::scene::Scene& scene);

    [[nodiscard]] static bool IsMapEntity(const kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept;
    // Only the legacy map mesh cache belongs to the tilemap; other meshes can coexist.
    [[nodiscard]] static bool IsMapMeshRenderer(const kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept;
    [[nodiscard]] static const kb::assets::AssetMetadata* MapMetadata(const kb::scene::Scene& scene, kb::scene::SceneEntity entity) noexcept;
    [[nodiscard]] static const kb::assets::AssetMetadata* MapMetadata(const kb::scene::Scene& scene, kb::assets::AssetId assetId) noexcept;
    // A new empty 64 x 64 map in Assets/Maps, with ECS components and a render preview.
    [[nodiscard]] static bool Create(
        kb::scene::Scene& scene,
        kb::scene::SceneEntity entity,
        const std::filesystem::path& assetsRoot,
        std::string* error = nullptr);
    [[nodiscard]] static std::optional<kb::assets::Map2DAsset> Load(
        const kb::scene::Scene& scene,
        kb::assets::AssetId assetId,
        std::string* error = nullptr);
    // Every instance of a shared map needs the same authored collision geometry.
    [[nodiscard]] static bool SyncCollision(
        kb::scene::Scene& scene,
        kb::assets::AssetId assetId,
        const kb::assets::Map2DAsset& map,
        bool* changed = nullptr,
        std::string* error = nullptr);
    // Hands the scene the map's new mesh, so the viewport shows it at once; `persist` also writes
    // the file.
    [[nodiscard]] static bool Publish(
        kb::scene::Scene& scene,
        kb::assets::AssetId assetId,
        const kb::assets::Map2DAsset& map,
        bool persist,
        std::string* error = nullptr);
};

} // namespace kb::editor
