#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>

namespace kb::editor {

class EditorSceneMaterialAssetActions {
public:
    EditorSceneMaterialAssetActions() = delete;

    [[nodiscard]] static bool IsMaterialAsset(const kb::assets::AssetMetadata& metadata) noexcept;
    [[nodiscard]] static bool AssignMaterial(
        kb::scene::Scene& scene,
        kb::scene::SceneEntity entity,
        kb::assets::AssetId materialAssetId);
    [[nodiscard]] static bool AssignMaterialSlotOverride(
        kb::scene::Scene& scene,
        kb::scene::SceneEntity entity,
        std::uint32_t slotIndex,
        kb::assets::AssetId materialAssetId);
};

} // namespace kb::editor
