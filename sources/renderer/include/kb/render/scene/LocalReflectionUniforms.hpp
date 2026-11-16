#pragma once

#include "kb/render/scene/SceneLocalReflectionAtlas.hpp"
#include "kb/render/scene/SceneRenderTypes.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace kb::render {

// Every local probe reads the one atlas texture (SceneLocalReflectionAtlas), so the limit is the size of the
// probe tables in local_reflection.sh and the atlas, not a sampler stage per probe.
inline constexpr std::uint32_t kMaxSampledLocalReflectionProbes = kMaxSceneReflectionProbes;

[[nodiscard]] inline bool ValidLocalReflectionConfig(const SceneRenderIblConfig& config) noexcept {
    if (config.reflectionProbeCount > kMaxSampledLocalReflectionProbes) return false;
    for (std::uint32_t i = 0; i < config.reflectionProbeCount; ++i) {
        const auto& probe = config.reflectionProbes[i];
        if (probe.shape != SceneRenderReflectionProbeShape::Infinite && probe.shape != SceneRenderReflectionProbeShape::Box && probe.shape != SceneRenderReflectionProbeShape::Sphere) return false;
        if (!bgfx::isValid(probe.radianceTexture) || probe.maxMip == 0U) return false;
        for (unsigned axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(probe.position[axis]) || !std::isfinite(probe.extents[axis]) || probe.extents[axis] <= 0) return false;
        }
        if (!std::isfinite(probe.intensity) || probe.intensity < 0.0F || (probe.shape == SceneRenderReflectionProbeShape::Sphere &&
            (!std::isfinite(probe.influenceRadius) || probe.influenceRadius <= 0))) return false;
    }
    return true;
}

class LocalReflectionUniforms final {
public:
    using Layout = LocalReflectionAtlasLayout;

    [[nodiscard]] bool Initialize() {
        if (IsInitialized()) return true;
        sampler_ = bgfx::createUniform("s_localReflection",bgfx::UniformType::Sampler);
        positions_ = bgfx::createUniform("u_localReflectionPositions",bgfx::UniformType::Vec4,kMaxSampledLocalReflectionProbes);
        extents_ = bgfx::createUniform("u_localReflectionExtents",bgfx::UniformType::Vec4,kMaxSampledLocalReflectionProbes);
        parameters_ = bgfx::createUniform("u_localReflectionParameters",bgfx::UniformType::Vec4,kMaxSampledLocalReflectionProbes);
        levels_ = bgfx::createUniform("u_localReflectionLevels",bgfx::UniformType::Vec4,Layout::kLevelCount);
        atlas_ = bgfx::createUniform("u_localReflectionAtlas",bgfx::UniformType::Vec4);
        control_ = bgfx::createUniform("u_localReflectionControl",bgfx::UniformType::Vec4);
        // Bound while no probe is ready, so the stage never holds a texture of another kind.
        const std::array<std::uint8_t,4> black{0U,0U,0U,255U};
        fallback_ = bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::RGBA8,0U,bgfx::copy(black.data(),static_cast<std::uint32_t>(black.size())));
        if (!IsInitialized()) { Shutdown(); return false; }
        return true;
    }
    void Shutdown() noexcept {
        if (bgfx::isValid(fallback_)) { bgfx::destroy(fallback_); fallback_ = BGFX_INVALID_HANDLE; }
        for (auto* handle : {&sampler_,&positions_,&extents_,&parameters_,&levels_,&atlas_,&control_}) {
            if (bgfx::isValid(*handle)) { bgfx::destroy(*handle); *handle = BGFX_INVALID_HANDLE; }
        }
    }
    [[nodiscard]] bool IsInitialized() const noexcept {
        return bgfx::isValid(sampler_) && bgfx::isValid(positions_) && bgfx::isValid(extents_) && bgfx::isValid(parameters_) &&
            bgfx::isValid(levels_) && bgfx::isValid(atlas_) && bgfx::isValid(control_) && bgfx::isValid(fallback_);
    }
    // Probes that are not ready in the atlas (or all of them, without an atlas) have no weight in the shader.
    void Set(const SceneRenderIblConfig& config, std::uint8_t stage) const {
        constexpr std::size_t kCount = kMaxSampledLocalReflectionProbes;
        std::array<float,kCount*4> positions{}, extents{}, parameters{};
        const auto count = std::min(config.reflectionProbeCount,kMaxSampledLocalReflectionProbes);
        const bool hasAtlas = bgfx::isValid(config.reflectionAtlas);
        for (unsigned i = 0; i < count; ++i) {
            const auto& probe = config.reflectionProbes[i];
            const bool enabled = hasAtlas && probe.IsValid() && probe.atlasReady;
            for (unsigned axis = 0; axis < 3; ++axis) {
                positions[i*4+axis] = probe.position[axis];
                extents[i*4+axis] = probe.extents[axis];
            }
            positions[i*4+3] = enabled ? probe.intensity : 0.0F;
            extents[i*4+3] = static_cast<float>(probe.shape);
            parameters[i*4] = static_cast<float>(probe.atlasSlot);
            parameters[i*4+1] = probe.influenceRadius;
            parameters[i*4+2] = probe.parallaxCorrection && config.parallaxCorrection ? 1.0F : 0.0F;
        }
        std::array<float,Layout::kLevelCount*4> levels{};
        for (std::uint32_t level = 0; level < Layout::kLevelCount; ++level) {
            levels[level*4] = static_cast<float>(Layout::LevelX(level));
            levels[level*4+1] = static_cast<float>(Layout::LevelY(level));
            levels[level*4+2] = static_cast<float>(Layout::LevelSize(level));
            levels[level*4+3] = static_cast<float>(Layout::kGutter);
        }
        const std::array<float,4> atlas{static_cast<float>(Layout::SlotWidth()),static_cast<float>(Layout::SlotHeight()),
            1.0F/static_cast<float>(Layout::Width()),1.0F/static_cast<float>(Layout::Height())};
        const std::array<float,4> control{static_cast<float>(count),config.localProbeBlending ? 1.0F : 0.0F,
            static_cast<float>(Layout::kColumns),static_cast<float>(Layout::kLevelCount - 1U)};
        bgfx::setTexture(stage,sampler_,hasAtlas ? config.reflectionAtlas : fallback_);
        bgfx::setUniform(positions_,positions.data(),kCount);
        bgfx::setUniform(extents_,extents.data(),kCount);
        bgfx::setUniform(parameters_,parameters.data(),kCount);
        bgfx::setUniform(levels_,levels.data(),Layout::kLevelCount);
        bgfx::setUniform(atlas_,atlas.data());
        bgfx::setUniform(control_,control.data());
    }
private:
    bgfx::UniformHandle sampler_ = BGFX_INVALID_HANDLE, positions_ = BGFX_INVALID_HANDLE, extents_ = BGFX_INVALID_HANDLE,
        parameters_ = BGFX_INVALID_HANDLE, levels_ = BGFX_INVALID_HANDLE, atlas_ = BGFX_INVALID_HANDLE, control_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle fallback_ = BGFX_INVALID_HANDLE;
};

} // namespace kb::render
