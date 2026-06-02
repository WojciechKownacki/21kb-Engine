#include "scene/prefab/ScenePrefabComponentComparator.hpp"

#include "engine/scene/SceneComponents.hpp"

namespace kb::scene {
namespace {

[[nodiscard]] bool Equal(Vec3 lhs, Vec3 rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

[[nodiscard]] bool Equal(const CameraComponent& lhs, const CameraComponent& rhs) noexcept {
    return lhs.projection == rhs.projection
        && lhs.verticalFovDegrees == rhs.verticalFovDegrees
        && lhs.orthographicHeight == rhs.orthographicHeight
        && lhs.nearClip == rhs.nearClip
        && lhs.farClip == rhs.farClip
        && lhs.primary == rhs.primary;
}

[[nodiscard]] bool Equal(const MeshRendererComponent& lhs, const MeshRendererComponent& rhs) noexcept {
    return lhs.meshAssetId == rhs.meshAssetId
        && lhs.materialAssetId == rhs.materialAssetId
        && lhs.materialSlotAssetIds == rhs.materialSlotAssetIds
        && lhs.materialSlotOverrideCount == rhs.materialSlotOverrideCount
        && lhs.castsShadow == rhs.castsShadow
        && lhs.receivesShadow == rhs.receivesShadow;
}

[[nodiscard]] bool Equal(const LightComponent& lhs, const LightComponent& rhs) noexcept {
    return lhs.kind == rhs.kind
        && Equal(lhs.color, rhs.color)
        && lhs.intensity == rhs.intensity
        && lhs.range == rhs.range
        && lhs.innerConeDegrees == rhs.innerConeDegrees
        && lhs.outerConeDegrees == rhs.outerConeDegrees;
}

template <typename T>
[[nodiscard]] bool EqualOptionalComponent(const T* actual, const std::optional<T>& expected) noexcept {
    if (actual == nullptr) {
        return !expected.has_value();
    }
    return expected.has_value() && Equal(*actual, *expected);
}

} // namespace

ScenePrefabOverrideFlag ScenePrefabComponentComparator::Compare(SceneComponents components, SceneEntity entity, const ScenePrefabNodeComponents& expected) noexcept {
    ScenePrefabOverrideFlag flags = ScenePrefabOverrideFlag::None;
    if (!EqualOptionalComponent(components.Cameras().TryGet(entity), expected.camera)) {
        flags |= ScenePrefabOverrideFlag::Camera;
    }
    if (!EqualOptionalComponent(components.MeshRenderers().TryGet(entity), expected.meshRenderer)) {
        flags |= ScenePrefabOverrideFlag::MeshRenderer;
    }
    if (!EqualOptionalComponent(components.Lights().TryGet(entity), expected.light)) {
        flags |= ScenePrefabOverrideFlag::Light;
    }
    return flags;
}

} // namespace kb::scene
