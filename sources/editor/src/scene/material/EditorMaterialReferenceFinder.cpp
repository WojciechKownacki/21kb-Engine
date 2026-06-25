#include "scene/material/EditorMaterialReferenceFinder.hpp"

#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponentQueries.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneVisitors.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <algorithm>
#include <utility>

namespace kb::editor {
namespace {

struct MaterialReferenceScan {
    const kb::scene::Scene* scene = nullptr;
    kb::assets::AssetId target{};
    std::vector<std::string> references;
};

void CollectMaterialReference(kb::scene::SceneEntity entity, const kb::scene::TransformComponent&, const kb::scene::MeshRendererComponent& renderer, void* context) {
    auto* scan = static_cast<MaterialReferenceScan*>(context);
    const std::string entityName = scan->scene->Entities().Name(entity);
    if (renderer.materialAssetId == scan->target.value) {
        scan->references.push_back(entityName + " / Mesh Renderer / Material");
    }
    const std::uint32_t slotCount = std::min(renderer.materialSlotOverrideCount, kb::scene::kMaxMeshRendererMaterialSlotOverrides);
    for (std::uint32_t slot = 0U; slot < slotCount; ++slot) {
        if (renderer.materialSlotAssetIds[slot] == scan->target.value) {
            scan->references.push_back(entityName + " / Mesh Renderer / Slot " + std::to_string(slot));
        }
    }
}

} // namespace

std::vector<std::string> EditorMaterialReferenceFinder::FindSceneReferences(const kb::scene::Scene& scene, kb::assets::AssetId materialAssetId) {
    MaterialReferenceScan scan{ .scene = &scene, .target = materialAssetId };
    scene.Components().Visitors().ForEachMeshRenderer(&CollectMaterialReference, &scan);
    return std::move(scan.references);
}

} // namespace kb::editor
