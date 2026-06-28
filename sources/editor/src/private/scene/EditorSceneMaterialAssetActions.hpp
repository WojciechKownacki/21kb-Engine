#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>

namespace kb::editor {

class EditorSceneMaterialAssetActions {
public:
    EditorSceneMaterialAssetActions() = delete;

    [[nodiscard]] static bool IsMaterialAsset(const kb::assets::AssetMetadata& metadata) noexcept;
    [[nodiscard]] static bool IsMaterialGraphAsset(const kb::assets::AssetMetadata& metadata) noexcept;
    [[nodiscard]] static const char* MaterialGraphAssignmentRejectionMessage() noexcept;
    [[nodiscard]] static bool AssignMaterial(
        kb::scene::Scene& scene,
        kb::scene::SceneEntity entity,
        kb::assets::AssetId materialAssetId);
    [[nodiscard]] static bool AssignMaterialToAllSlots(
        kb::scene::Scene& scene,
        kb::scene::SceneEntity entity,
        kb::assets::AssetId materialAssetId);
    [[nodiscard]] static bool AssignMaterialSlotOverride(
        kb::scene::Scene& scene,
        kb::scene::SceneEntity entity,
        std::uint32_t slotIndex,
        kb::assets::AssetId materialAssetId);
    static void CleanupMaterialSlotOverrides(
        kb::scene::MeshRendererComponent& renderer,
        std::uint32_t materialSlotCount) noexcept;
};

} // namespace kb::editor
