#pragma once

#include "engine/scene/ParticleEffectAsset.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace kb::particle_editor {

enum class ParticleEditorComposerSection : std::uint8_t {
    Emitters,
    Settings,
    Recipes,
    Modules,
    Output,
    Dependencies,
    Diagnostics,
};

class ParticleEditorWorkspaceState final {
public:
    void Synchronize(const kb::scene::ParticleEffectAsset& asset) noexcept;
    [[nodiscard]] bool Select(const kb::scene::ParticleEffectAsset& asset,
                              kb::scene::ParticleStableId emitterId) noexcept;
    [[nodiscard]] kb::scene::ParticleStableId SelectedEmitterId() const noexcept;

    void SetFocused(bool focused) noexcept;
    [[nodiscard]] bool Focused() const noexcept;

    void SetComposerScrollOffset(int offset) noexcept;
    [[nodiscard]] int ComposerScrollOffset() const noexcept;
    [[nodiscard]] bool ComposerSectionExpanded(ParticleEditorComposerSection section) const noexcept;
    void ToggleComposerSection(ParticleEditorComposerSection section) noexcept;

    [[nodiscard]] bool BeginRename(const kb::scene::ParticleEffectAsset& asset,
                                   kb::scene::ParticleStableId emitterId);
    void AppendRenameText(std::string_view text);
    void RemoveRenameCharacter() noexcept;
    void CancelRename() noexcept;
    [[nodiscard]] bool RenameActive() const noexcept;
    [[nodiscard]] kb::scene::ParticleStableId RenameEmitterId() const noexcept;
    [[nodiscard]] const std::string& RenameText() const noexcept;

    [[nodiscard]] bool BeginEmitterDrag(const kb::scene::ParticleEffectAsset& asset,
                                        kb::scene::ParticleStableId emitterId) noexcept;
    void UpdateEmitterDrag(std::uint32_t targetOrder) noexcept;
    void EndEmitterDrag() noexcept;
    [[nodiscard]] bool EmitterDragActive() const noexcept;
    [[nodiscard]] kb::scene::ParticleStableId DraggedEmitterId() const noexcept;
    [[nodiscard]] std::uint32_t DragTargetOrder() const noexcept;

    [[nodiscard]] bool SelectModule(const kb::scene::ParticleEffectAsset& asset,
                                    kb::scene::ParticleStableId emitterId,
                                    kb::scene::ParticleStableId moduleId) noexcept;
    void ClearSelectedModule() noexcept;
    [[nodiscard]] kb::scene::ParticleStableId SelectedModuleId() const noexcept;
    [[nodiscard]] bool BeginModuleDrag(const kb::scene::ParticleEffectAsset& asset,
                                       kb::scene::ParticleStableId emitterId,
                                       kb::scene::ParticleStableId moduleId) noexcept;
    void UpdateModuleDrag(std::uint32_t targetOrder) noexcept;
    void EndModuleDrag() noexcept;
    [[nodiscard]] bool ModuleDragActive() const noexcept;
    [[nodiscard]] kb::scene::ParticleStableId DraggedModuleId() const noexcept;
    [[nodiscard]] std::uint32_t ModuleDragTargetOrder() const noexcept;
    void FocusDiagnostic(std::string propertyPath, kb::scene::ParticleStableId emitterId,
                         kb::scene::ParticleStableId moduleId) noexcept;
    [[nodiscard]] const std::string& FocusedPropertyPath() const noexcept;

private:
    kb::scene::ParticleStableId selectedEmitterId_ = 0U;
    bool focused_ = false;
    int composerScrollOffset_ = 0;
    bool emittersExpanded_ = true;
    bool settingsExpanded_ = true;
    bool recipesExpanded_ = true;
    bool modulesExpanded_ = true;
    bool outputExpanded_ = true;
    bool dependenciesExpanded_ = false;
    bool diagnosticsExpanded_ = true;
    kb::scene::ParticleStableId renameEmitterId_ = 0U;
    std::string renameText_;
    kb::scene::ParticleStableId draggedEmitterId_ = 0U;
    std::uint32_t dragTargetOrder_ = 0U;
    kb::scene::ParticleStableId selectedModuleId_ = 0U;
    kb::scene::ParticleStableId draggedModuleId_ = 0U;
    std::uint32_t moduleDragTargetOrder_ = 0U;
    std::string focusedPropertyPath_;
};

} // namespace kb::particle_editor
