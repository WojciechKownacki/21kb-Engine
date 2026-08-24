#pragma once

#include "engine/scene/ParticleEffectAsset.hpp"
#include "engine/scene/ParticleEffectAssetValidation.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace kb::scene {

inline constexpr const char* kParticleEffectAssetExtension = ".kbvfx";
inline constexpr const char* kParticleEffectAssetType = "ParticleEffect";

struct ParticleEffectLoadResult {
    std::optional<ParticleEffectAsset> asset;
    std::vector<ParticleEffectDiagnostic> diagnostics;
    bool migratedFromLegacy = false;

    [[nodiscard]] bool Succeeded() const noexcept {
        return asset.has_value() && !ParticleEffectDiagnosticsHaveErrors(diagnostics);
    }
};

struct ParticleEffectSaveResult {
    std::vector<ParticleEffectDiagnostic> diagnostics;
    [[nodiscard]] bool Succeeded() const noexcept {
        return !ParticleEffectDiagnosticsHaveErrors(diagnostics);
    }
};

class ParticleEffectAssetIO final {
  public:
    ParticleEffectAssetIO() = delete;

    [[nodiscard]] static ParticleEffectLoadResult LoadDetailed(const std::filesystem::path& path);
    [[nodiscard]] static ParticleEffectLoadResult Parse(std::string_view source);
    [[nodiscard]] static std::optional<ParticleEffectAsset> Load(const std::filesystem::path& path);
    [[nodiscard]] static ParticleEffectSaveResult SaveDetailed(const std::filesystem::path& path,
                                                               const ParticleEffectAsset& asset);
    [[nodiscard]] static bool Save(const std::filesystem::path& path, const ParticleEffectAsset& asset);
    [[nodiscard]] static std::optional<std::string> Serialize(const ParticleEffectAsset& asset,
                                                              std::vector<ParticleEffectDiagnostic>& diagnostics);
};

} // namespace kb::scene
