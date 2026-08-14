#pragma once

#include "ParticleEditorDocument.hpp"
#include "ParticleEditorWorkspaceState.hpp"

namespace kb::particle_editor {

class ParticleEditorCommands final {
public:
    ParticleEditorCommands() = delete;

    [[nodiscard]] static ParticleEditorResult AddEmitter(
        ParticleEditorDocument& document,
        ParticleEditorWorkspaceState& workspace,
        kb::scene::ParticleAssetReference material);
    [[nodiscard]] static ParticleEditorResult RenameEmitter(
        ParticleEditorDocument& document,
        ParticleEditorWorkspaceState& workspace,
        kb::scene::ParticleStableId emitterId,
        std::string name);
    [[nodiscard]] static ParticleEditorResult SetEmitterEnabled(
        ParticleEditorDocument& document,
        ParticleEditorWorkspaceState& workspace,
        kb::scene::ParticleStableId emitterId,
        bool enabled);
    [[nodiscard]] static ParticleEditorResult ReorderEmitter(
        ParticleEditorDocument& document,
        ParticleEditorWorkspaceState& workspace,
        kb::scene::ParticleStableId emitterId,
        std::uint32_t targetOrder);
    [[nodiscard]] static ParticleEditorResult RemoveEmitter(
        ParticleEditorDocument& document,
        ParticleEditorWorkspaceState& workspace,
        kb::scene::ParticleStableId emitterId);
    [[nodiscard]] static ParticleEditorResult SetEmitterSpawn(
        ParticleEditorDocument& document, ParticleEditorWorkspaceState& workspace,
        kb::scene::ParticleStableId emitterId, kb::scene::ParticleSpawnAsset spawn);
    [[nodiscard]] static ParticleEditorResult SetEmitterOutput(
        ParticleEditorDocument& document, ParticleEditorWorkspaceState& workspace,
        kb::scene::ParticleStableId emitterId, kb::scene::ParticleOutputAsset output);
    [[nodiscard]] static ParticleEditorResult AddModule(
        ParticleEditorDocument& document, ParticleEditorWorkspaceState& workspace,
        kb::scene::ParticleStableId emitterId, kb::scene::ParticleModuleType type);
    [[nodiscard]] static ParticleEditorResult SetModuleEnabled(
        ParticleEditorDocument& document, ParticleEditorWorkspaceState& workspace,
        kb::scene::ParticleStableId emitterId, kb::scene::ParticleStableId moduleId, bool enabled);
    [[nodiscard]] static ParticleEditorResult SetModulePayload(
        ParticleEditorDocument& document, ParticleEditorWorkspaceState& workspace,
        kb::scene::ParticleStableId emitterId, kb::scene::ParticleStableId moduleId,
        kb::scene::ParticleModulePayload payload);
    [[nodiscard]] static ParticleEditorResult ReorderModule(
        ParticleEditorDocument& document, ParticleEditorWorkspaceState& workspace,
        kb::scene::ParticleStableId emitterId, kb::scene::ParticleStableId moduleId,
        std::uint32_t targetOrder);
    [[nodiscard]] static ParticleEditorResult RemoveModule(
        ParticleEditorDocument& document, ParticleEditorWorkspaceState& workspace,
        kb::scene::ParticleStableId emitterId, kb::scene::ParticleStableId moduleId);
};

} // namespace kb::particle_editor
