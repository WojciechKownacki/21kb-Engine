#include "renderer/RendererSceneLightingConfigResolver.hpp"

namespace kb::render {

SceneRenderLightingConfig RendererSceneLightingConfigResolver::Resolve(SceneRenderLightingConfig requested, SceneRenderLightingConfig fallback) noexcept {
    constexpr SceneRenderLightingConfig defaultConfig{};
    return SceneRenderLightingConfig{
        .maxForwardLights = requested.maxForwardLights != defaultConfig.maxForwardLights ? requested.maxForwardLights : fallback.maxForwardLights,
        .lightingPath = requested.lightingPath != defaultConfig.lightingPath ? requested.lightingPath : fallback.lightingPath,
        .clusterDimensions = requested.clusterDimensions != defaultConfig.clusterDimensions ? requested.clusterDimensions : fallback.clusterDimensions,
        .ambientColor = requested.ambientColor != defaultConfig.ambientColor ? requested.ambientColor : fallback.ambientColor,
        .ambientIntensity = requested.ambientIntensity != defaultConfig.ambientIntensity ? requested.ambientIntensity : fallback.ambientIntensity,
        .environmentMode = requested.environmentMode != defaultConfig.environmentMode ? requested.environmentMode : fallback.environmentMode,
        .environmentZenithColor = requested.environmentZenithColor != defaultConfig.environmentZenithColor ? requested.environmentZenithColor : fallback.environmentZenithColor,
        .environmentGroundColor = requested.environmentGroundColor != defaultConfig.environmentGroundColor ? requested.environmentGroundColor : fallback.environmentGroundColor,
        .environmentDiffuseIntensity = requested.environmentDiffuseIntensity != defaultConfig.environmentDiffuseIntensity ? requested.environmentDiffuseIntensity : fallback.environmentDiffuseIntensity,
        .environmentSpecularIntensity = requested.environmentSpecularIntensity != defaultConfig.environmentSpecularIntensity ? requested.environmentSpecularIntensity : fallback.environmentSpecularIntensity,
        .ibl = requested.ibl.HasEnvironment() || requested.ibl.reflectionProbeCount != 0U ? requested.ibl : fallback.ibl,
        .globalIllumination = requested.globalIllumination != defaultConfig.globalIllumination ? requested.globalIllumination : fallback.globalIllumination,
        .shadowMapSize = requested.shadowMapSize != defaultConfig.shadowMapSize ? requested.shadowMapSize : fallback.shadowMapSize,
        .shadowCascadeCount = requested.shadowCascadeCount != defaultConfig.shadowCascadeCount ? requested.shadowCascadeCount : fallback.shadowCascadeCount,
        .shadowAtlasSize = requested.shadowAtlasSize != defaultConfig.shadowAtlasSize ? requested.shadowAtlasSize : fallback.shadowAtlasSize,
        .shadowDistance = requested.shadowDistance != defaultConfig.shadowDistance ? requested.shadowDistance : fallback.shadowDistance,
        .shadowDepthBias = requested.shadowDepthBias != defaultConfig.shadowDepthBias ? requested.shadowDepthBias : fallback.shadowDepthBias,
        .shadowStrength = requested.shadowStrength != defaultConfig.shadowStrength ? requested.shadowStrength : fallback.shadowStrength,
        .shadowFilter = requested.shadowFilter != defaultConfig.shadowFilter ? requested.shadowFilter : fallback.shadowFilter,
        .shadowsEnabled = requested.shadowsEnabled != defaultConfig.shadowsEnabled ? requested.shadowsEnabled : fallback.shadowsEnabled,
        .stableShadowCascades = requested.stableShadowCascades != defaultConfig.stableShadowCascades ? requested.stableShadowCascades : fallback.stableShadowCascades,
        .perLightShadowCaching = requested.perLightShadowCaching != defaultConfig.perLightShadowCaching ? requested.perLightShadowCaching : fallback.perLightShadowCaching,
        .contactShadowsEnabled = requested.contactShadowsEnabled != defaultConfig.contactShadowsEnabled ? requested.contactShadowsEnabled : fallback.contactShadowsEnabled,
        .volumetricLightingEnabled = requested.volumetricLightingEnabled != defaultConfig.volumetricLightingEnabled ? requested.volumetricLightingEnabled : fallback.volumetricLightingEnabled,
    };
}

std::uint32_t RendererSceneLightingConfigResolver::ShadowFilterSampleCount(SceneRenderShadowFilter filter) noexcept {
    switch (filter) {
    case SceneRenderShadowFilter::Hard:
        return 1U;
    case SceneRenderShadowFilter::Pcf3x3:
        return 9U;
    case SceneRenderShadowFilter::Evsm:
        return 4U;
    case SceneRenderShadowFilter::Msm:
        return 4U;
    case SceneRenderShadowFilter::Pcss:
        return 16U;
    }
    return 9U;
}

std::uint32_t RendererSceneLightingConfigResolver::EnvironmentSampleCount(SceneRenderEnvironmentMode mode) noexcept {
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

} // namespace kb::render
