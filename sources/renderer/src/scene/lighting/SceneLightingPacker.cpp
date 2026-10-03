#include "scene/lighting/SceneLightingPacker.hpp"

#include "scene/lighting/SceneForwardLightSelector.hpp"
#include "scene/lighting/SceneLightShaderData.hpp"

#include "engine/math/EngineMath.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace kb::render {
namespace {

bool CopyLight(const ScenePackedLight& packed, std::uint32_t slot, PackedSceneLighting& lighting) noexcept {
    if (slot >= kMaxSceneForwardPlusLights) return false;
    const std::array<float*, 5> destinations{lighting.dirKind.data(), lighting.positionRange.data(),
        lighting.colorIntensity.data(), lighting.spot.data(), lighting.areaRight.data()};
    for (std::size_t index = 0U; index < destinations.size(); ++index)
        std::copy(packed.texels[index].begin(), packed.texels[index].end(), destinations[index] + slot * 4U);
    return true;
}

bool PackLight(const LightRenderProxyDesc& light, std::uint32_t slot, PackedSceneLighting& lighting) noexcept {
    return CopyLight(SceneLightShaderData::Pack(light), slot, lighting);
}

bool PackEditorPreviewKeyLight(SceneRenderLightingConfig config, std::uint32_t slot, PackedSceneLighting& lighting) noexcept {
    const auto preview = SceneLightShaderData::EditorPreview(config);
    return preview && CopyLight(*preview, slot, lighting);
}

[[nodiscard]] std::uint32_t ClampedForwardLightBudget(SceneRenderLightingConfig config) noexcept {
    if (config.maxForwardLights == 0U) {
        return 0U;
    }
    const std::uint32_t maxSupported = config.lightingPath == SceneRenderLightingPath::Forward
        ? kMaxSceneForwardLights
        : kMaxSceneForwardPlusLights;
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

void FillIblStats(SceneRenderSubmitStats& stats, SceneRenderLightingConfig config) noexcept {
    stats.lightingPath = static_cast<std::uint32_t>(config.lightingPath) + 1U;
    stats.lightingPathProduction = IsSceneRenderLightingPathProduction(config.lightingPath);
    // Actual grid statistics are published by its GPU resource owner after upload.
    stats.lightClusterCount = 0U;
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

void SceneLightingPacker::AssignPointShadowSlots(PackedSceneLighting& lighting, const ScenePointShadowBinding& binding) noexcept {
    lighting.pointShadowSlot.fill(-1.0F);
    const std::uint32_t packedCount = std::min<std::uint32_t>(
        static_cast<std::uint32_t>(lighting.params[0]), kMaxSceneForwardPlusLights);
    for (std::uint32_t shadow = 0U; shadow < binding.lightCount; ++shadow) {
        for (std::uint32_t slot = 0U; slot < packedCount; ++slot) {
            if (lighting.slotEntityId[slot] == binding.entityId[shadow]) {
                lighting.pointShadowSlot[shadow] = static_cast<float>(slot);
                break;
            }
        }
    }
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
    if (selection.selectedCount != 0U) lighting.primaryLightId = selection.selected[0].entityId;
    std::uint32_t submittedSceneLightCount = 0U;
    for (std::uint32_t slot = 0U; slot < selection.selectedCount; ++slot) {
        if (selection.selected[slot].light != nullptr && PackLight(*selection.selected[slot].light, slot, lighting)) {
            lighting.slotEntityId[slot] = selection.selected[slot].entityId;
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
