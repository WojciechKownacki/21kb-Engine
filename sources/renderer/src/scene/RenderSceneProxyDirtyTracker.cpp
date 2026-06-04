#include "RenderSceneProxyDirtyTracker.hpp"

namespace kb::render {

RenderProxyDirtyFlag RenderSceneProxyDirtyTracker::DirtyForMeshChange(const MeshRenderProxyDesc& current, const MeshRenderProxyDesc& next) noexcept {
    RenderProxyDirtyFlag dirty = RenderProxyDirtyFlag::None;
    if (current.model != next.model) {
        dirty |= RenderProxyDirtyFlag::Transform;
    }
    if (current.meshAssetId != next.meshAssetId || current.castsShadow != next.castsShadow || current.receivesShadow != next.receivesShadow) {
        dirty |= RenderProxyDirtyFlag::Mesh;
    }
    if (current.materialAssetId != next.materialAssetId ||
        current.materialSlotAssetIds != next.materialSlotAssetIds ||
        current.materialSlotOverrideCount != next.materialSlotOverrideCount ||
        current.color != next.color) {
        dirty |= RenderProxyDirtyFlag::Material;
    }
    if (current.visible != next.visible) {
        dirty |= RenderProxyDirtyFlag::Visibility;
    }
    return dirty;
}

RenderProxyDirtyFlag RenderSceneProxyDirtyTracker::DirtyForCameraChange(const CameraRenderProxyDesc& current, const CameraRenderProxyDesc& next) noexcept {
    RenderProxyDirtyFlag dirty = RenderProxyDirtyFlag::None;
    if (current.position != next.position || current.rotation != next.rotation) {
        dirty |= RenderProxyDirtyFlag::Transform;
    }
    if (current.projection != next.projection ||
        current.verticalFovDegrees != next.verticalFovDegrees ||
        current.orthographicHeight != next.orthographicHeight ||
        current.nearClip != next.nearClip ||
        current.farClip != next.farClip ||
        current.primary != next.primary) {
        dirty |= RenderProxyDirtyFlag::Camera;
    }
    if (current.visible != next.visible) {
        dirty |= RenderProxyDirtyFlag::Visibility;
    }
    return dirty;
}

RenderProxyDirtyFlag RenderSceneProxyDirtyTracker::DirtyForLightChange(const LightRenderProxyDesc& current, const LightRenderProxyDesc& next) noexcept {
    RenderProxyDirtyFlag dirty = RenderProxyDirtyFlag::None;
    if (current.position != next.position || current.rotation != next.rotation) {
        dirty |= RenderProxyDirtyFlag::Transform;
    }
    if (current.kind != next.kind ||
        current.color != next.color ||
        current.intensity != next.intensity ||
        current.range != next.range ||
        current.innerConeDegrees != next.innerConeDegrees ||
        current.outerConeDegrees != next.outerConeDegrees ||
        current.areaWidth != next.areaWidth ||
        current.areaHeight != next.areaHeight ||
        current.contactShadowLength != next.contactShadowLength ||
        current.volumetricScattering != next.volumetricScattering ||
        current.castsShadow != next.castsShadow) {
        dirty |= RenderProxyDirtyFlag::Light;
    }
    if (current.visible != next.visible) {
        dirty |= RenderProxyDirtyFlag::Visibility;
    }
    return dirty;
}

} // namespace kb::render
