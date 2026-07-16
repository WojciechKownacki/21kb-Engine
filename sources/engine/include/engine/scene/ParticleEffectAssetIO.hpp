#pragma once

#include "engine/scene/ParticleEffectAsset.hpp"

#include <filesystem>
#include <optional>
#include <string_view>

namespace kb::scene {

inline constexpr const char* kParticleEffectAssetExtension = ".kbvfx";
inline constexpr const char* kParticleEffectAssetType = "ParticleEffect";

// LIB-143: a flat `key value\n` text format, mirroring
// kb::render::PostProcessProfileAssetLoader's own established convention for simple
// (non-graph) assets - one line per scalar field, repeatable `sizeCurveKeyframe`/
// `colorGradientStop` lines build up the Curve/Gradient, unknown keys are ignored (forward
// compatible), a missing key leaves ParticleEffectAsset's own default member value in place.
class ParticleEffectAssetIO final {
public:
    ParticleEffectAssetIO() = delete;

    [[nodiscard]] static std::optional<ParticleEffectAsset> Load(const std::filesystem::path& path);
    [[nodiscard]] static bool Save(const std::filesystem::path& path, const ParticleEffectAsset& asset);
};

} // namespace kb::scene
