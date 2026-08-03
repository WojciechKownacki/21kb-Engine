#include "RenderSceneProxyConverters.hpp"

#include "kb/render/SceneDepthPolicy.hpp"

#include "engine/math/EngineMath.hpp"

#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace kb::render {
namespace {

[[nodiscard]] float Aspect(std::uint32_t width, std::uint32_t height) noexcept {
    return height == 0U ? 1.0F : static_cast<float>(std::max(1U, width)) / static_cast<float>(height);
}

// LIB-044: delegates to the single canonical kb::math::ToRadians instead
// of an independently-rederived degrees-to-radians constant (this file
// used to hardcode pi/180 itself, one of 6+ copies of the same formula
// scattered across sources/renderer and sources/editor).
[[nodiscard]] float DegreesToRadians(float degrees) noexcept {
    return kb::math::ToRadians(kb::math::Degrees{ degrees }).Value();
}

struct Basis {
    float xx = 1.0F;
    float xy = 0.0F;
    float xz = 0.0F;
    float yx = 0.0F;
    float yy = 1.0F;
    float yz = 0.0F;
    float zx = 0.0F;
    float zy = 0.0F;
    float zz = 1.0F;
};

[[nodiscard]] Basis BasisFromQuat(const std::array<float, 4>& q) noexcept {
    const float x = q[0];
    const float y = q[1];
    const float z = q[2];
    const float w = q[3];
    const float x2 = x + x;
    const float y2 = y + y;
    const float z2 = z + z;
    const float xx = x * x2;
    const float xy = x * y2;
    const float xz = x * z2;
    const float yy = y * y2;
    const float yz = y * z2;
    const float zz = z * z2;
    const float wx = w * x2;
    const float wy = w * y2;
    const float wz = w * z2;

    return Basis{
        .xx = 1.0F - (yy + zz),
        .xy = xy + wz,
        .xz = xz - wy,
        .yx = xy - wz,
        .yy = 1.0F - (xx + zz),
        .yz = yz + wx,
        .zx = xz + wy,
        .zy = yz - wx,
        .zz = 1.0F - (xx + yy),
    };
}

[[nodiscard]] SceneRenderCameraClearMode ResolvedClearModeOf(RenderCameraClearMode clearMode) noexcept {
    switch (clearMode) {
    case RenderCameraClearMode::SolidColor:
        return SceneRenderCameraClearMode::SolidColor;
    case RenderCameraClearMode::DepthOnly:
        return SceneRenderCameraClearMode::DepthOnly;
    case RenderCameraClearMode::DontClear:
        return SceneRenderCameraClearMode::DontClear;
    }
    return SceneRenderCameraClearMode::SolidColor;
}

} // namespace

SceneRenderCamera RenderSceneCameraBuilder::Build(const CameraRenderProxyDesc& camera, std::uint32_t viewportWidth, std::uint32_t viewportHeight) {
    const Basis basis = BasisFromQuat(camera.rotation);
    const bx::Vec3 eye{ camera.position[0], camera.position[1], camera.position[2] };
    const bx::Vec3 at{
        camera.position[0] + basis.zx,
        camera.position[1] + basis.zy,
        camera.position[2] + basis.zz,
    };
    const bx::Vec3 up{ basis.yx, basis.yy, basis.yz };

    SceneRenderCamera renderCamera{
        .cullingMask = camera.cullingMask,
        .clearMode = ResolvedClearModeOf(camera.clearMode),
        .clearColor = camera.clearColor,
    };
    bx::mtxLookAt(renderCamera.view.data(), eye, at, up);
    const bool homogeneousDepth = SceneDepthPolicy::HomogeneousDepth();
    switch (camera.projection) {
    case RenderCameraProjection::Perspective:
        SceneDepthPolicy::MakePerspective(
            renderCamera.projection.data(),
            camera.verticalFovDegrees,
            Aspect(viewportWidth, viewportHeight),
            camera.nearClip,
            camera.farClip,
            homogeneousDepth);
        break;
    case RenderCameraProjection::Orthographic:
        SceneDepthPolicy::MakeOrthographic(
            renderCamera.projection.data(),
            camera.orthographicHeight,
            Aspect(viewportWidth, viewportHeight),
            camera.nearClip,
            camera.farClip,
            homogeneousDepth);
        break;
    }
    return renderCamera;
}

SceneRenderMeshInstance RenderSceneMeshInstanceBuilder::Build(const MeshRenderProxyDesc& mesh) noexcept {
    return SceneRenderMeshInstance{
        .entityId = mesh.entityId,
        .meshAssetId = mesh.meshAssetId,
        .materialAssetId = mesh.materialAssetId,
        .materialSlotAssetIds = mesh.materialSlotAssetIds,
        .materialSlotOverrideCount = mesh.materialSlotOverrideCount,
        .model = mesh.model,
        .color = mesh.color,
        .fadeAmount = mesh.fadeAmount,
        .customData0 = mesh.customData0,
        .currentSkinningPalette = mesh.currentSkinningPalette,
        .previousSkinningPalette = mesh.previousSkinningPalette,
        .castsShadow = mesh.castsShadow,
        .receivesShadow = mesh.receivesShadow,
        .layer = mesh.layer,
        .detailSwitchGroupId = mesh.detailSwitchGroupId,
        .detailSwitchMinimumLod = mesh.detailSwitchMinimumLod,
        .detailSwitchMaximumLod = mesh.detailSwitchMaximumLod,
        .detailSwitchPromoteCoverage = mesh.detailSwitchPromoteCoverage,
        .detailSwitchDemoteCoverage = mesh.detailSwitchDemoteCoverage,
        .detailSwitchEnabled = mesh.detailSwitchEnabled,
    };
}

SceneRenderLight RenderSceneLightBuilder::Build(const LightRenderProxyDesc& light) noexcept {
    const Basis basis = BasisFromQuat(light.rotation);
    return SceneRenderLight{
        .entityId = light.entityId,
        .kind = light.kind,
        .position = { light.position[0], light.position[1], light.position[2] },
        .direction = { basis.zx, basis.zy, basis.zz },
        .right = { basis.xx, basis.xy, basis.xz },
        .color = { light.color[0], light.color[1], light.color[2] },
        .intensity = light.intensity,
        .range = light.range,
        .innerConeCos = std::cos(DegreesToRadians(light.innerConeDegrees)),
        .outerConeCos = std::cos(DegreesToRadians(light.outerConeDegrees)),
        .areaWidth = light.areaWidth,
        .areaHeight = light.areaHeight,
        .contactShadowLength = light.contactShadowLength,
        .volumetricScattering = light.volumetricScattering,
        .castsShadow = light.castsShadow,
    };
}

} // namespace kb::render
