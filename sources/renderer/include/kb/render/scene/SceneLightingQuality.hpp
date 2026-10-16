#pragma once

#include "engine/project/ProjectSettings.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"

#include <cstdint>

namespace kb::render {

// The shadow and global illumination detail a project pins for one device class. Auto changes nothing: the
// lighting config keeps what the engine and the scene chose. Every other level overwrites the settings that
// decide what shadows and bounce light cost, from the cheapest (Low) to the most detailed (Ultra), with
// smaller maps, fewer cascades and a coarser voxel grid on phones and tablets.
inline void ApplyProjectLightingQuality(
    SceneRenderLightingConfig& config,
    project::ProjectQualityLevel shadows,
    project::ProjectQualityLevel globalIllumination,
    bool mobile) noexcept {
    using Level = project::ProjectQualityLevel;
    struct ShadowPreset {
        std::uint32_t mapSize;
        std::uint32_t cascades;
        std::uint32_t atlasSize;
        float distance;
        SceneRenderShadowFilter filter;
    };
    constexpr ShadowPreset kDesktopShadows[]{
        { 1024U, 2U, 1024U, 80.0F, SceneRenderShadowFilter::Pcf3x3 },
        { 2048U, 3U, 2048U, 200.0F, SceneRenderShadowFilter::Pcf3x3 },
        { 2048U, 4U, 4096U, 500.0F, SceneRenderShadowFilter::Pcf3x3 },
        { 4096U, 4U, 4096U, 1000.0F, SceneRenderShadowFilter::Pcss },
    };
    constexpr ShadowPreset kMobileShadows[]{
        { 512U, 2U, 1024U, 40.0F, SceneRenderShadowFilter::Hard },
        { 1024U, 2U, 1024U, 100.0F, SceneRenderShadowFilter::Pcf3x3 },
        { 2048U, 3U, 2048U, 200.0F, SceneRenderShadowFilter::Pcf3x3 },
        { 2048U, 4U, 2048U, 400.0F, SceneRenderShadowFilter::Pcf3x3 },
    };
    if (shadows != Level::Auto) {
        const ShadowPreset& preset = (mobile ? kMobileShadows : kDesktopShadows)[static_cast<std::size_t>(shadows) - 1U];
        config.shadowMapSize = preset.mapSize;
        config.shadowCascadeCount = preset.cascades;
        config.shadowAtlasSize = preset.atlasSize;
        config.shadowDistance = preset.distance;
        config.shadowFilter = preset.filter;
    }

    struct GiPreset {
        float voxelSize;
        float historyWeight;
    };
    constexpr GiPreset kDesktopGi[]{ { 1.0F, 0.94F }, { 0.75F, 0.92F }, { 0.5F, 0.9F }, { 0.35F, 0.9F } };
    constexpr GiPreset kMobileGi[]{ { 1.5F, 0.95F }, { 1.0F, 0.93F }, { 0.75F, 0.92F }, { 0.5F, 0.9F } };
    if (globalIllumination != Level::Auto) {
        const GiPreset& preset = (mobile ? kMobileGi : kDesktopGi)[static_cast<std::size_t>(globalIllumination) - 1U];
        config.giVoxelSize = preset.voxelSize;
        config.giHistoryWeight = preset.historyWeight;
    }
}

} // namespace kb::render
