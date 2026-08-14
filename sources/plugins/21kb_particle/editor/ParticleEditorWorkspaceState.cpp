#include "ParticleEditorWorkspaceState.hpp"

#include "ParticleEmitterListModel.hpp"

#include <algorithm>

namespace kb::particle_editor {

void ParticleEditorWorkspaceState::Synchronize(const kb::scene::ParticleEffectAsset& asset) noexcept {
    if (ParticleEmitterListModel::Find(asset, selectedEmitterId_) != nullptr)
        return;
    selectedEmitterId_ = 0U;
    if (!asset.emitters.empty()) {
        const auto first = std::min_element(asset.emitters.begin(), asset.emitters.end(),
            [](const auto& left, const auto& right) {
                return left.authoringOrder < right.authoringOrder;
            });
        selectedEmitterId_ = first->emitterId;
    }
    CancelRename();
    EndEmitterDrag();
}

bool ParticleEditorWorkspaceState::Select(const kb::scene::ParticleEffectAsset& asset,
                                          kb::scene::ParticleStableId emitterId) noexcept {
    if (ParticleEmitterListModel::Find(asset, emitterId) == nullptr)
        return false;
    selectedEmitterId_ = emitterId;
    if (renameEmitterId_ != emitterId)
        CancelRename();
    return true;
}

kb::scene::ParticleStableId ParticleEditorWorkspaceState::SelectedEmitterId() const noexcept {
    return selectedEmitterId_;
}

void ParticleEditorWorkspaceState::SetFocused(bool focused) noexcept { focused_ = focused; }
bool ParticleEditorWorkspaceState::Focused() const noexcept { return focused_; }

void ParticleEditorWorkspaceState::SetComposerScrollOffset(int offset) noexcept {
    composerScrollOffset_ = std::max(0, offset);
}

int ParticleEditorWorkspaceState::ComposerScrollOffset() const noexcept { return composerScrollOffset_; }

bool ParticleEditorWorkspaceState::BeginRename(const kb::scene::ParticleEffectAsset& asset,
                                               kb::scene::ParticleStableId emitterId) {
    const kb::scene::ParticleEmitterAsset* emitter = ParticleEmitterListModel::Find(asset, emitterId);
    if (emitter == nullptr)
        return false;
    selectedEmitterId_ = emitterId;
    renameEmitterId_ = emitterId;
    renameText_ = emitter->name;
    return true;
}

void ParticleEditorWorkspaceState::AppendRenameText(std::string_view text) {
    if (renameEmitterId_ != 0U &&
        text.size() <= kb::scene::kParticleEffectMaxStringBytes - renameText_.size())
        renameText_.append(text);
}

void ParticleEditorWorkspaceState::RemoveRenameCharacter() noexcept {
    if (renameText_.empty())
        return;
    std::size_t begin = renameText_.size() - 1U;
    while (begin > 0U &&
           (static_cast<unsigned char>(renameText_[begin]) & 0xc0U) == 0x80U)
        --begin;
    renameText_.erase(begin);
}

void ParticleEditorWorkspaceState::CancelRename() noexcept {
    renameEmitterId_ = 0U;
    renameText_.clear();
}

bool ParticleEditorWorkspaceState::RenameActive() const noexcept { return renameEmitterId_ != 0U; }
kb::scene::ParticleStableId ParticleEditorWorkspaceState::RenameEmitterId() const noexcept {
    return renameEmitterId_;
}
const std::string& ParticleEditorWorkspaceState::RenameText() const noexcept { return renameText_; }

bool ParticleEditorWorkspaceState::BeginEmitterDrag(const kb::scene::ParticleEffectAsset& asset,
                                                    kb::scene::ParticleStableId emitterId) noexcept {
    const kb::scene::ParticleEmitterAsset* emitter = ParticleEmitterListModel::Find(asset, emitterId);
    if (emitter == nullptr)
        return false;
    selectedEmitterId_ = emitterId;
    draggedEmitterId_ = emitterId;
    dragTargetOrder_ = emitter->authoringOrder;
    CancelRename();
    return true;
}

void ParticleEditorWorkspaceState::UpdateEmitterDrag(std::uint32_t targetOrder) noexcept {
    if (draggedEmitterId_ != 0U)
        dragTargetOrder_ = targetOrder;
}

void ParticleEditorWorkspaceState::EndEmitterDrag() noexcept {
    draggedEmitterId_ = 0U;
    dragTargetOrder_ = 0U;
}

bool ParticleEditorWorkspaceState::EmitterDragActive() const noexcept { return draggedEmitterId_ != 0U; }
kb::scene::ParticleStableId ParticleEditorWorkspaceState::DraggedEmitterId() const noexcept {
    return draggedEmitterId_;
}
std::uint32_t ParticleEditorWorkspaceState::DragTargetOrder() const noexcept { return dragTargetOrder_; }

} // namespace kb::particle_editor
