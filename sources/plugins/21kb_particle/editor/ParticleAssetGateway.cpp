#include "ParticleAssetGateway.hpp"

#include "engine/scene/ParticleEffectAssetIO.hpp"

#include <utility>

namespace kb::particle_editor {

ParticleAssetLoadResult ParticleAssetGateway::Load(const std::filesystem::path& path) const {
    auto loaded = kb::scene::ParticleEffectAssetIO::LoadDetailed(path);
    if (!loaded.Succeeded()) {
        return { .result = { .status = ParticleEditorStatus::IoFailure,
                             .message = "particle document could not be loaded",
                             .diagnostics = std::move(loaded.diagnostics) } };
    }
    auto validation = kb::scene::ParticleEffectAssetValidator::ValidateStructure(*loaded.asset);
    if (!validation.Succeeded()) {
        return { .result = { .status = ParticleEditorStatus::InvalidAsset,
                             .message = "particle document failed structural validation",
                             .diagnostics = std::move(validation.diagnostics) } };
    }
    return { .asset = std::move(loaded.asset) };
}

ParticleEditorResult ParticleAssetGateway::Save(
    const std::filesystem::path& path,
    const kb::scene::ParticleEffectAsset& asset) const {
    auto validation = kb::scene::ParticleEffectAssetValidator::ValidateStructure(asset);
    if (!validation.Succeeded()) {
        return { .status = ParticleEditorStatus::InvalidAsset,
                 .message = "particle document failed structural validation",
                 .diagnostics = std::move(validation.diagnostics) };
    }
    auto saved = kb::scene::ParticleEffectAssetIO::SaveDetailed(path, asset);
    if (!saved.Succeeded()) {
        return { .status = ParticleEditorStatus::IoFailure,
                 .message = "particle document atomic save failed",
                 .diagnostics = std::move(saved.diagnostics) };
    }
    return {};
}

} // namespace kb::particle_editor
