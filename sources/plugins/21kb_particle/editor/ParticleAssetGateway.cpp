#include "ParticleAssetGateway.hpp"

#include "engine/scene/ParticleEffectAssetIO.hpp"

#include <utility>
#include <algorithm>

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

ParticleEditorResult ParticleAssetGateway::SaveNewProjectEffect(
    const std::filesystem::path& path, const std::filesystem::path& virtualPath,
    kb::scene::ParticleEffectAsset asset, const std::filesystem::path& defaultMaterialSource) const {
    constexpr std::string_view defaultPath = "/21kbParticle/Materials/DefaultParticle.kbmat";
    if (!virtualPath.generic_string().starts_with("/Game/") || virtualPath.lexically_normal() != virtualPath ||
        virtualPath.filename() != path.filename()) return {.status = ParticleEditorStatus::InvalidAsset,
            .message = "new particle effect requires its canonical project virtual path"};
    const auto usesDefault = [defaultPath](const auto& emitter) { return emitter.output.material.virtualPath == defaultPath; };
    auto validation = kb::scene::ParticleEffectAssetValidator::ValidateStructure(asset);
    if (!validation.Succeeded()) return {.status = ParticleEditorStatus::InvalidAsset,
        .message = "particle document failed structural validation", .diagnostics = std::move(validation.diagnostics)};
    std::error_code error;
    if (std::filesystem::exists(path, error) || error) return {.status = ParticleEditorStatus::IoFailure,
        .message = "particle effect destination already exists or cannot be inspected"};
    if (std::none_of(asset.emitters.begin(), asset.emitters.end(), usesDefault)) return Save(path, asset);
    if (!std::filesystem::is_regular_file(defaultMaterialSource, error) || error) return {
        .status = ParticleEditorStatus::IoFailure, .message = "shared default particle material is unavailable"};
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return {.status = ParticleEditorStatus::IoFailure, .message = "particle effect directory could not be created"};
    std::filesystem::path material;
    for (std::uint32_t suffix = 0U; suffix < 128U; ++suffix) {
        auto name = path.stem();
        name += "Material";
        if (suffix != 0U) name += std::to_string(suffix);
        name += ".kbmat";
        const auto candidate = path.parent_path() / name;
        if (std::filesystem::copy_file(defaultMaterialSource, candidate, std::filesystem::copy_options::none, error)) {
            material = candidate;
            break;
        }
        if (error != std::errc::file_exists) return {.status = ParticleEditorStatus::IoFailure,
            .message = "default particle material could not be imported: " + error.message()};
        error.clear();
    }
    if (material.empty()) return {.status = ParticleEditorStatus::IoFailure,
        .message = "default particle material destinations are occupied"};
    for (auto& emitter : asset.emitters) {
        if (usesDefault(emitter)) emitter.output.material = {
            .assetId = 0U, .virtualPath = (virtualPath.parent_path() / material.filename()).generic_string()};
    }
    auto result = Save(path, asset);
    if (!result.Succeeded()) static_cast<void>(std::filesystem::remove(material, error));
    return result;
}

} // namespace kb::particle_editor
