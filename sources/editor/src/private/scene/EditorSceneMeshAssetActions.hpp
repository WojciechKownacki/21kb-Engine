#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <cstdint>
#include <string_view>

namespace kb::editor {

class EditorSceneMeshAssetActions {
public:
    EditorSceneMeshAssetActions() = delete;

    [[nodiscard]] static bool IsMeshAsset(const kb::assets::AssetMetadata& metadata) noexcept;
    [[nodiscard]] static bool IsScenePlaceableAsset(const kb::assets::AssetMetadata& metadata) noexcept;
    [[nodiscard]] static bool AssignMesh(
        kb::scene::Scene& scene,
        kb::scene::SceneEntity entity,
        kb::assets::AssetId meshAssetId);
    [[nodiscard]] static kb::scene::SceneEntity CreateMeshEntity(
        kb::scene::Scene& scene,
        kb::assets::AssetId meshAssetId,
        std::string_view name,
        kb::scene::Vec3 position = {});
    [[nodiscard]] static kb::scene::SceneEntity CreateSkeletalMeshEntity(
        kb::scene::Scene& scene,
        kb::assets::AssetId meshAssetId,
        kb::assets::AssetId skeletonAssetId,
        std::uint64_t skeletonCompatibilitySignature,
        std::string_view name,
        kb::scene::Vec3 position = {});
};

} // namespace kb::editor
