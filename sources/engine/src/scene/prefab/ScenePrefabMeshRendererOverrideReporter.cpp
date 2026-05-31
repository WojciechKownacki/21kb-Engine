#include "scene/prefab/ScenePrefabMeshRendererOverrideReporter.hpp"

#include "scene/prefab/ScenePrefabOverridePropertyReporter.hpp"
#include "scene/prefab/ScenePrefabOverrideValueFormatter.hpp"

namespace kb::scene {
namespace {

[[nodiscard]] bool Equal(const MeshRendererComponent& lhs, const MeshRendererComponent& rhs) noexcept {
    return lhs.meshAssetId == rhs.meshAssetId
        && lhs.materialAssetId == rhs.materialAssetId
        && lhs.castsShadow == rhs.castsShadow
        && lhs.receivesShadow == rhs.receivesShadow;
}

} // namespace

void ScenePrefabMeshRendererOverrideReporter::Append(SceneComponents components, SceneEntity entity, const std::optional<MeshRendererComponent>& expected, ScenePrefabOverrideReport& report, std::uint32_t nodeIndex, SceneObject object) {
    const MeshRendererComponent* actual = components.MeshRenderers().TryGet(entity);
    if (actual == nullptr && !expected.has_value()) {
        return;
    }
    if (actual != nullptr && expected.has_value() && Equal(*actual, *expected)) {
        return;
    }
    if (actual == nullptr || !expected.has_value()) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "meshRenderer", actual == nullptr ? "null" : "present", ScenePrefabOverrideFlag::MeshRenderer);
        return;
    }
    if (actual->meshAssetId != expected->meshAssetId) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "meshRenderer.meshAssetId", ScenePrefabOverrideValueFormatter::ToString(actual->meshAssetId), ScenePrefabOverrideFlag::MeshRenderer);
    }
    if (actual->materialAssetId != expected->materialAssetId) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "meshRenderer.materialAssetId", ScenePrefabOverrideValueFormatter::ToString(actual->materialAssetId), ScenePrefabOverrideFlag::MeshRenderer);
    }
    if (actual->castsShadow != expected->castsShadow) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "meshRenderer.castsShadow", ScenePrefabOverrideValueFormatter::ToString(actual->castsShadow), ScenePrefabOverrideFlag::MeshRenderer);
    }
    if (actual->receivesShadow != expected->receivesShadow) {
        ScenePrefabOverridePropertyReporter::Add(report, nodeIndex, object, "meshRenderer.receivesShadow", ScenePrefabOverrideValueFormatter::ToString(actual->receivesShadow), ScenePrefabOverrideFlag::MeshRenderer);
    }
}

} // namespace kb::scene
