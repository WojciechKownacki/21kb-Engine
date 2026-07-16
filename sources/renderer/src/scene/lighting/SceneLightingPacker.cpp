#include "scene/lighting/SceneLightingPacker.hpp"

#include "scene/lighting/SceneForwardLightSelector.hpp"

#include "engine/math/EngineMath.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace kb::render {
namespace {

struct Basis {
    float zx = 0.0F;
    float zy = 0.0F;
    float zz = 1.0F;
};

// LIB-044: delegates to the single canonical kb::math::ToRadians instead
// of an independently-rederived degrees-to-radians constant.
[[nodiscard]] float DegreesToRadians(float degrees) noexcept {
    return kb::math::ToRadians(kb::math::Degrees{ degrees }).Value();
}

[[nodiscard]] Basis BasisFromQuat(const std::array<float, 4>& q) noexcept {
    const float x = q[0];
    const float y = q[1];
    const float z = q[2];
    const float w = q[3];
    const float x2 = x + x;
    const float y2 = y + y;
    const float z2 = z + z;
    const float xz = x * z2;
    const float yy = y * y2;
    const float yz = y * z2;
    const float wx = w * x2;
    const float wy = w * y2;
    const float xx = x * x2;

    return Basis{
        .zx = xz + wy,
        .zy = yz - wx,
        .zz = 1.0F - (xx + yy),
    };
}

void Normalize(float& x, float& y, float& z) noexcept {
    const float length = std::sqrt(x * x + y * y + z * z);
    if (length <= 0.0001F) {
        x = 0.0F;
        y = 0.0F;
        z = 1.0F;
        return;
    }

    x /= length;
    y /= length;
    z /= length;
}

[[nodiscard]] float LightKindValue(RenderLightKind kind) noexcept {
    switch (kind) {
    case RenderLightKind::Directional:
        return 0.0F;
    case RenderLightKind::Point:
        return 1.0F;
    case RenderLightKind::Spot:
        return 2.0F;
    case RenderLightKind::AreaRect:
        return 3.0F;
    case RenderLightKind::AreaDisk:
        return 4.0F;
    case RenderLightKind::Tube:
        return 5.0F;
    }
    return 1.0F;
}

bool PackLight(const LightRenderProxyDesc& light, std::uint32_t slot, PackedSceneLighting& lighting) noexcept {
    if (slot >= kMaxSceneForwardPlusLights) {
        return false;
    }

    const std::uint32_t offset = slot * 4U;
    Basis basis = BasisFromQuat(light.rotation);
    Normalize(basis.zx, basis.zy, basis.zz);

    lighting.dirKind[offset + 0U] = basis.zx;
    lighting.dirKind[offset + 1U] = basis.zy;
    lighting.dirKind[offset + 2U] = basis.zz;
    lighting.dirKind[offset + 3U] = LightKindValue(light.kind);
    lighting.positionRange[offset + 0U] = light.position[0];
    lighting.positionRange[offset + 1U] = light.position[1];
    lighting.positionRange[offset + 2U] = light.position[2];
    lighting.positionRange[offset + 3U] = std::max(light.range, 0.0F);
    lighting.colorIntensity[offset + 0U] = std::max(light.color[0], 0.0F);
    lighting.colorIntensity[offset + 1U] = std::max(light.color[1], 0.0F);
    lighting.colorIntensity[offset + 2U] = std::max(light.color[2], 0.0F);
    lighting.colorIntensity[offset + 3U] = light.intensity;

    const float innerCos = std::cos(DegreesToRadians(light.innerConeDegrees));
    const float outerCos = std::cos(DegreesToRadians(light.outerConeDegrees));
    lighting.spot[offset + 0U] = std::max(innerCos, outerCos);
    lighting.spot[offset + 1U] = std::min(innerCos, outerCos);
    lighting.spot[offset + 2U] = 0.0F;
    lighting.spot[offset + 3U] = 0.0F;
    return true;
}

bool PackEditorPreviewKeyLight(SceneRenderLightingConfig config, std::uint32_t slot, PackedSceneLighting& lighting) noexcept {
    if (!config.editorPreviewKeyLightEnabled || config.editorPreviewKeyLightIntensity <= 0.0F || slot >= kMaxSceneForwardPlusLights) {
        return false;
    }

    float x = config.editorPreviewKeyLightDirection[0];
    float y = config.editorPreviewKeyLightDirection[1];
    float z = config.editorPreviewKeyLightDirection[2];
    Normalize(x, y, z);

    const std::uint32_t offset = slot * 4U;
    lighting.dirKind[offset + 0U] = x;
    lighting.dirKind[offset + 1U] = y;
    lighting.dirKind[offset + 2U] = z;
    lighting.dirKind[offset + 3U] = LightKindValue(RenderLightKind::Directional);
    lighting.positionRange[offset + 0U] = 0.0F;
    lighting.positionRange[offset + 1U] = 0.0F;
    lighting.positionRange[offset + 2U] = 0.0F;
    lighting.positionRange[offset + 3U] = 0.0F;
    lighting.colorIntensity[offset + 0U] = std::max(config.editorPreviewKeyLightColor[0], 0.0F);
    lighting.colorIntensity[offset + 1U] = std::max(config.editorPreviewKeyLightColor[1], 0.0F);
    lighting.colorIntensity[offset + 2U] = std::max(config.editorPreviewKeyLightColor[2], 0.0F);
    lighting.colorIntensity[offset + 3U] = std::max(config.editorPreviewKeyLightIntensity, 0.0F);
    return true;
}

[[nodiscard]] std::uint32_t ClampedForwardLightBudget(SceneRenderLightingConfig config) noexcept {
    if (config.maxForwardLights == 0U) {
        return 0U;
    }
    const std::uint32_t maxSupported = config.lightingPath == SceneRenderLightingPath::ClusteredForwardPlus
        ? kMaxSceneForwardPlusLights
        : kMaxSceneForwardLights;
    return std::min<std::uint32_t>(config.maxForwardLights, maxSupported);
}

[[nodiscard]] float EnvironmentModeValue(SceneRenderEnvironmentMode mode) noexcept {
    switch (mode) {
    case SceneRenderEnvironmentMode::Disabled:
        return 0.0F;
    case SceneRenderEnvironmentMode::Constant:
        return 1.0F;
    case SceneRenderEnvironmentMode::Hemisphere:
        return 2.0F;
    case SceneRenderEnvironmentMode::ImageBased:
        return 3.0F;
    }
    return 1.0F;
}

[[nodiscard]] std::uint32_t EnvironmentSampleCount(SceneRenderEnvironmentMode mode) noexcept {
    switch (mode) {
    case SceneRenderEnvironmentMode::Disabled:
        return 0U;
    case SceneRenderEnvironmentMode::Constant:
        return 1U;
    case SceneRenderEnvironmentMode::Hemisphere:
        return 2U;
    case SceneRenderEnvironmentMode::ImageBased:
        return 4U;
    }
    return 1U;
}

[[nodiscard]] std::uint32_t ClusterCount(SceneRenderLightingConfig config) noexcept {
    if (config.lightingPath != SceneRenderLightingPath::ClusteredForwardPlus) {
        return 0U;
    }
    return static_cast<std::uint32_t>(config.clusterDimensions[0]) *
        static_cast<std::uint32_t>(config.clusterDimensions[1]) *
        static_cast<std::uint32_t>(config.clusterDimensions[2]);
}

void FillIblStats(SceneRenderSubmitStats& stats, SceneRenderLightingConfig config) noexcept {
    stats.lightingPath = static_cast<std::uint32_t>(config.lightingPath) + 1U;
    stats.lightingPathProduction = IsSceneRenderLightingPathProduction(config.lightingPath);
    stats.lightClusterCount = ClusterCount(config);
    stats.globalIlluminationMode = static_cast<std::uint32_t>(config.globalIllumination) + 1U;
    const std::uint32_t probeCount = std::min<std::uint32_t>(config.ibl.reflectionProbeCount, kMaxSceneReflectionProbes);
    stats.reflectionProbeCount = probeCount;
    for (std::uint32_t probeIndex = 0U; probeIndex < probeCount; ++probeIndex) {
        const SceneRenderReflectionProbe& probe = config.ibl.reflectionProbes[probeIndex];
        if (probe.shape != SceneRenderReflectionProbeShape::Infinite) {
            ++stats.localReflectionProbeCount;
        }
        if (probe.parallaxCorrection || config.ibl.parallaxCorrection) {
            ++stats.parallaxCorrectedProbeCount;
        }
    }
}

} // namespace

std::array<float, 4> SceneLightingPacker::CameraPosition(const SceneRenderCamera* camera) noexcept {
    if (camera == nullptr) {
        return { 0.0F, 0.0F, 0.0F, 1.0F };
    }
    const std::array<float, 16>& view = camera->view;
    const float tx = view[12];
    const float ty = view[13];
    const float tz = view[14];
    return {
        -(view[0] * tx + view[1] * ty + view[2] * tz),
        -(view[4] * tx + view[5] * ty + view[6] * tz),
        -(view[8] * tx + view[9] * ty + view[10] * tz),
        1.0F,
    };
}

PackedSceneLighting SceneLightingPacker::Build(
    const RenderScene& renderScene,
    SceneRenderSubmitStats& stats,
    SceneRenderLightingConfig config,
    const SceneRenderCamera* camera) noexcept {
    PackedSceneLighting lighting{};
    const std::uint32_t capacity = ClampedForwardLightBudget(config);
    lighting.params[1] = static_cast<float>(capacity);
    const float ambientIntensity = std::max(config.ambientIntensity, 0.0F);
    lighting.ambient = {
        std::max(config.ambientColor[0], 0.0F) * ambientIntensity,
        std::max(config.ambientColor[1], 0.0F) * ambientIntensity,
        std::max(config.ambientColor[2], 0.0F) * ambientIntensity,
        1.0F,
    };
    lighting.environmentZenith = {
        std::max(config.environmentZenithColor[0], 0.0F) * ambientIntensity,
        std::max(config.environmentZenithColor[1], 0.0F) * ambientIntensity,
        std::max(config.environmentZenithColor[2], 0.0F) * ambientIntensity,
        1.0F,
    };
    lighting.environmentGround = {
        std::max(config.environmentGroundColor[0], 0.0F) * ambientIntensity,
        std::max(config.environmentGroundColor[1], 0.0F) * ambientIntensity,
        std::max(config.environmentGroundColor[2], 0.0F) * ambientIntensity,
        1.0F,
    };
    lighting.environmentParams = {
        EnvironmentModeValue(config.environmentMode),
        std::max(config.environmentDiffuseIntensity, 0.0F),
        std::max(config.environmentSpecularIntensity, 0.0F),
        0.0F,
    };
    stats.submittedEnvironmentLightingCount = config.environmentMode == SceneRenderEnvironmentMode::Disabled ? 0U : 1U;
    stats.environmentLightingMode = static_cast<std::uint32_t>(config.environmentMode) + 1U;
    stats.environmentLightingSampleCount = EnvironmentSampleCount(config.environmentMode);
    FillIblStats(stats, config);
    stats.sceneLightCount = static_cast<std::uint32_t>(renderScene.LightProxies().size());
    stats.forwardLightCapacity = capacity;
    const std::array<float, 4> cameraPosition = CameraPosition(camera);
    const std::uint32_t cameraCullingMask = camera != nullptr ? camera->cullingMask : 0xFFFFFFFFU;
    const SceneForwardLightSelection selection = SceneForwardLightSelector::Select(renderScene.LightProxies(), capacity, cameraPosition, stats, config, cameraCullingMask);
    std::uint32_t submittedSceneLightCount = 0U;
    for (std::uint32_t slot = 0U; slot < selection.selectedCount; ++slot) {
        if (selection.selected[slot].light != nullptr && PackLight(*selection.selected[slot].light, slot, lighting)) {
            ++stats.submittedForwardLightCount;
            ++submittedSceneLightCount;
        }
    }
    if (stats.submittedForwardLightCount < capacity &&
        PackEditorPreviewKeyLight(config, stats.submittedForwardLightCount, lighting)) {
        ++stats.submittedForwardLightCount;
    }
    stats.skippedForwardLightCount = selection.validLightCount - submittedSceneLightCount;
    lighting.params[0] = static_cast<float>(stats.submittedForwardLightCount);
    return lighting;
}

} // namespace kb::render
