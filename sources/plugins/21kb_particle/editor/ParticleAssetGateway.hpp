#pragma once

#include "ParticleEditorDocument.hpp"

#include <filesystem>
#include <optional>

namespace kb::particle_editor {

struct ParticleAssetLoadResult {
    ParticleEditorResult result;
    std::optional<kb::scene::ParticleEffectAsset> asset;
};

class ParticleAssetGateway final {
public:
    [[nodiscard]] ParticleAssetLoadResult Load(const std::filesystem::path& path) const;
    [[nodiscard]] ParticleEditorResult Save(const std::filesystem::path& path,
                                            const kb::scene::ParticleEffectAsset& asset) const;
};

} // namespace kb::particle_editor
