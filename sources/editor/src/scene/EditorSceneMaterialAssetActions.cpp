#include "scene/EditorSceneMaterialAssetActions.hpp"

#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/render/resources/RenderMeshAssetBuilder.hpp"

#include <algorithm>
#include <optional>

namespace kb::editor {
namespace {

[[nodiscard]] std::optional<std::uint32_t> MeshMaterialSlotCount(
    kb::scene::Scene& scene,
    const kb::scene::MeshRendererComponent& renderer) {
    if (renderer.meshAssetId == 0U) {
        return std::nullopt;
    }
    const kb::assets::AssetHandle<kb::render::RenderMeshAssetData> mesh =
        scene.Assets().Manager().Load<kb::render::RenderMeshAssetData>(kb::assets::AssetId{ renderer.meshAssetId });
    if (!mesh.IsLoaded()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(std::min<std::size_t>(mesh->materialSlots.size(), kb::scene::kMaxMeshRendererMaterialSlotOverrides));
}

[[nodiscard]] bool IsKnownRawMaterialGraphAsset(kb::scene::Scene& scene, kb::assets::AssetId assetId) noexcept {
    if (!assetId.IsValid()) {
        return false;
    }
    const kb::assets::AssetMetadata* metadata = scene.Assets().Manager().Registry().Find(assetId);
    return metadata != nullptr && EditorSceneMaterialAssetActions::IsMaterialGraphAsset(*metadata);
}

} // namespace

bool EditorSceneMaterialAssetActions::IsMaterialAsset(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == "RenderMaterial" || metadata.type == "RenderMaterialInstance";
}

bool EditorSceneMaterialAssetActions::IsMaterialGraphAsset(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == "RenderMaterialGraph" || metadata.type == "MaterialGraph" || metadata.importCategory == "MaterialGraph";
}

const char* EditorSceneMaterialAssetActions::MaterialGraphAssignmentRejectionMessage() noexcept {
    return "Material Graph assets cannot be assigned to Mesh Renderers directly. Use Create Material From Graph first, then assign the generated .kbmat material.";
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
    if (IsKnownRawMaterialGraphAsset(scene, materialAssetId)) {
        return false;
    }

    renderer->materialAssetId = materialAssetId.value;
    scene.Components().MeshRenderers().MarkModified(entity);
    return true;
}

bool EditorSceneMaterialAssetActions::AssignMaterialToAllSlots(
    kb::scene::Scene& scene,
    kb::scene::SceneEntity entity,
    kb::assets::AssetId materialAssetId) {
    if (!AssignMaterial(scene, entity, materialAssetId)) {
        return false;
    }

    kb::scene::MeshRendererComponent* renderer = scene.Components().MeshRenderers().TryGet(entity);
    if (renderer == nullptr) {
        return false;
    }
    CleanupMaterialSlotOverrides(*renderer, 0U);
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
    if (IsKnownRawMaterialGraphAsset(scene, materialAssetId)) {
        return false;
    }
    const std::optional<std::uint32_t> materialSlotCount = MeshMaterialSlotCount(scene, *renderer);
    if (materialSlotCount.has_value() && slotIndex >= *materialSlotCount) {
        CleanupMaterialSlotOverrides(*renderer, *materialSlotCount);
        scene.Components().MeshRenderers().MarkModified(entity);
        return false;
    }

    renderer->materialSlotAssetIds[slotIndex] = materialAssetId.value;
    renderer->materialSlotOverrideCount = std::max(renderer->materialSlotOverrideCount, slotIndex + 1U);
    CleanupMaterialSlotOverrides(*renderer, materialSlotCount.value_or(kb::scene::kMaxMeshRendererMaterialSlotOverrides));
    scene.Components().MeshRenderers().MarkModified(entity);
    return true;
}

void EditorSceneMaterialAssetActions::CleanupMaterialSlotOverrides(
    kb::scene::MeshRendererComponent& renderer,
    std::uint32_t materialSlotCount) noexcept {
    const std::uint32_t clampedSlotCount = std::min(materialSlotCount, kb::scene::kMaxMeshRendererMaterialSlotOverrides);
    for (std::uint32_t index = clampedSlotCount; index < kb::scene::kMaxMeshRendererMaterialSlotOverrides; ++index) {
        renderer.materialSlotAssetIds[index] = 0U;
    }
    renderer.materialSlotOverrideCount = std::min(renderer.materialSlotOverrideCount, clampedSlotCount);
    while (renderer.materialSlotOverrideCount > 0U) {
        const std::uint32_t last = renderer.materialSlotOverrideCount - 1U;
        if (renderer.materialSlotAssetIds[last] != 0U) {
            break;
        }
        --renderer.materialSlotOverrideCount;
    }
}

} // namespace kb::editor
