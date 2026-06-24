#include "scene/EditorSceneMaterialAssetActions.hpp"

#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"

#include <algorithm>

namespace kb::editor {
namespace {

void TrimMaterialSlotOverrides(kb::scene::MeshRendererComponent& renderer) noexcept {
    while (renderer.materialSlotOverrideCount > 0U) {
        const std::uint32_t last = renderer.materialSlotOverrideCount - 1U;
        if (last >= kb::scene::kMaxMeshRendererMaterialSlotOverrides || renderer.materialSlotAssetIds[last] != 0U) {
            break;
        }
        --renderer.materialSlotOverrideCount;
    }
}

} // namespace

bool EditorSceneMaterialAssetActions::IsMaterialAsset(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == "RenderMaterial" || metadata.type == "RenderMaterialInstance";
}

bool EditorSceneMaterialAssetActions::AssignMaterial(
    kb::scene::Scene& scene,
    kb::scene::SceneEntity entity,
    kb::assets::AssetId materialAssetId) {
    if (!scene.Entities().IsAlive(entity)) {
        return false;
    }

    kb::scene::MeshRendererComponent* renderer = scene.Components().MeshRenderers().TryGet(entity);
    if (renderer == nullptr) {
        return false;
    }

    renderer->materialAssetId = materialAssetId.value;
    scene.Components().MeshRenderers().MarkModified(entity);
    return true;
}

bool EditorSceneMaterialAssetActions::AssignMaterialSlotOverride(
    kb::scene::Scene& scene,
    kb::scene::SceneEntity entity,
    std::uint32_t slotIndex,
    kb::assets::AssetId materialAssetId) {
    if (!scene.Entities().IsAlive(entity) || slotIndex >= kb::scene::kMaxMeshRendererMaterialSlotOverrides) {
        return false;
    }

    kb::scene::MeshRendererComponent* renderer = scene.Components().MeshRenderers().TryGet(entity);
    if (renderer == nullptr) {
        return false;
    }

    renderer->materialSlotAssetIds[slotIndex] = materialAssetId.value;
    renderer->materialSlotOverrideCount = std::max(renderer->materialSlotOverrideCount, slotIndex + 1U);
    TrimMaterialSlotOverrides(*renderer);
    scene.Components().MeshRenderers().MarkModified(entity);
    return true;
}

} // namespace kb::editor
