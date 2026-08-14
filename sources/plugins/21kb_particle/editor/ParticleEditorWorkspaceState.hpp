#pragma once

#include "engine/scene/ParticleEffectAsset.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace kb::particle_editor {

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

private:
    kb::scene::ParticleStableId selectedEmitterId_ = 0U;
    bool focused_ = false;
    int composerScrollOffset_ = 0;
    kb::scene::ParticleStableId renameEmitterId_ = 0U;
    std::string renameText_;
    kb::scene::ParticleStableId draggedEmitterId_ = 0U;
    std::uint32_t dragTargetOrder_ = 0U;
};

} // namespace kb::particle_editor
