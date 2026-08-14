#pragma once

#include "ParticleEffectCompiler.hpp"
#include "engine/assets/AssetId.hpp"
#include "engine/scene/ParticleEffectAssetValidation.hpp"

#include <string>
#include <vector>

namespace kb::assets {
class AssetRegistry;
struct AssetMetadata;
}

namespace kb::particle_editor {

enum class ParticleEditorProperty : std::uint16_t {
    SpawnRateSummary, SpawnBurstsSummary, SpawnLifetimeMin, SpawnLifetimeMax, SpawnSpeedMin, SpawnSpeedMax, SpawnDirection,
    SpawnSpreadDegrees, SpawnRandomization, SpawnPrewarmSeconds,
    OutputBlend, OutputSort, OutputDepthTest, OutputDepthWrite, OutputSoftParticles,
    OutputAntiAliasing, OutputAlignment, FlipbookColumns, FlipbookRows,
    FlipbookFramesPerSecond, FlipbookLooping, OutputVelocityScale, OutputMinimumLength,
    OutputPointDiameter, ModulePayload,
};

struct ParticleEditorPropertyRow {
    ParticleEditorProperty property = ParticleEditorProperty::SpawnLifetimeMin;
    kb::scene::ParticleStableId moduleId = 0U;
    std::uint8_t payloadField = 0U;
    std::string label;
    std::string value;
    bool editable = true;
};

struct ParticleOutputChoiceRow {
    kb::scene::ParticleOutputType type = kb::scene::ParticleOutputType::Billboard;
    std::string label;
    bool enabled = false;
    std::vector<kb::scene::ParticleEffectDiagnostic> diagnostics;
};

struct ParticleModuleStackRow {
    kb::scene::ParticleStableId moduleId = 0U;
    std::uint32_t authoringOrder = 0U;
    kb::scene::ParticleModuleType type = kb::scene::ParticleModuleType::InitialVelocity;
    std::string label;
    std::string summary;
    bool enabled = false;
    bool selected = false;
};

struct ParticleDependencyRow {
    kb::assets::AssetId assetId{};
    std::string virtualPath;
};

struct ParticleEmitterInspectorView {
    kb::scene::ParticleStableId emitterId = 0U;
    std::vector<ParticleOutputChoiceRow> outputChoices;
    std::vector<ParticleModuleStackRow> modules;
    std::vector<ParticleEditorPropertyRow> properties;
    std::vector<ParticleDependencyRow> dependencies;
    std::vector<kb::scene::ParticleEffectDiagnostic> diagnostics;
};

class ParticleEmitterInspectorModel final {
public:
    ParticleEmitterInspectorModel() = delete;
    [[nodiscard]] static ParticleEmitterInspectorView Build(
        const kb::scene::ParticleEffectAsset& asset,
        kb::scene::ParticleStableId selectedEmitterId,
        kb::scene::ParticleStableId selectedModuleId,
        const kb::assets::AssetMetadata* owner,
        const kb::assets::AssetRegistry* registry,
        const kb::particle_plugin::ParticleCompileRequest& request = {});
};

} // namespace kb::particle_editor
