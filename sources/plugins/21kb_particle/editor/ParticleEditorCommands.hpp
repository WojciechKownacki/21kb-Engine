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
};

} // namespace kb::particle_editor
