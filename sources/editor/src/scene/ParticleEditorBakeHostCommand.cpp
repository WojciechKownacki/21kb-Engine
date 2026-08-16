#include "scene/ParticleEditorBakeHostCommand.hpp"

#include "console/EditorConsoleState.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/scene/ParticleEffectAssetValidation.hpp"

#include <string>

namespace kb::editor {

kb::particle_editor::ParticleBakeResult ParticleEditorBakeHostCommand::Execute(
    const kb::scene::ParticleEffectAsset& workingAsset,
    const kb::assets::AssetMetadata& metadata,
    const kb::assets::AssetRegistry& registry,
    const std::filesystem::path& projectRoot,
    EditorConsoleState& console) {
    kb::particle_editor::ParticleBakeResult result = kb::particle_editor::ParticleBakeService::Bake({
        .workingAsset = workingAsset,
        .owner = metadata,
        .registry = registry,
        .cacheRoot = projectRoot / "Saved" / "21kbParticleCache",
    });
    if (result.Succeeded()) {
        const std::string disposition = result.status == kb::particle_editor::ParticleBakeStatus::Baked
            ? "Baked compiled particle effect: " : "Compiled particle effect is up to date: ";
        console.Info("Particles", disposition + result.cachePath.generic_string());
        return result;
    }
    if (result.diagnostics.empty()) {
        console.Error("Particles", "Particle Bake failed without a compiled artifact.");
        return result;
    }
    for (const kb::scene::ParticleEffectDiagnostic& diagnostic : result.diagnostics)
        console.Error("Particles", kb::scene::FormatParticleEffectDiagnostic(diagnostic));
    return result;
}

} // namespace kb::editor
