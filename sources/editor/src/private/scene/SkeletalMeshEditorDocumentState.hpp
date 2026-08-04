#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/scene/SkeletalMeshAsset.hpp"

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace kb::editor {

class SkeletalMeshEditorDocumentState {
public:
    void Open(kb::assets::AssetId assetId, kb::scene::SkeletalMeshAsset asset) {
        assetId_ = assetId;
        saved_ = std::move(asset);
        working_ = saved_;
        history_.clear();
        history_.push_back(*working_);
        historyCursor_ = 0U;
    }

    void Close() noexcept {
        assetId_ = {};
        saved_.reset();
        working_.reset();
        history_.clear();
        historyCursor_ = 0U;
    }

    [[nodiscard]] kb::assets::AssetId AssetId() const noexcept { return assetId_; }
    [[nodiscard]] bool IsOpen() const noexcept { return assetId_.IsValid() && working_.has_value(); }
    [[nodiscard]] bool Dirty() const noexcept { return IsOpen() && historyCursor_ != 0U; }
    [[nodiscard]] bool CanUndo() const noexcept { return historyCursor_ > 0U; }
    [[nodiscard]] bool CanRedo() const noexcept { return historyCursor_ + 1U < history_.size(); }
    [[nodiscard]] const kb::scene::SkeletalMeshAsset* WorkingCopy() const noexcept {
        return working_ ? &*working_ : nullptr;
    }

    [[nodiscard]] bool Apply(kb::scene::SkeletalMeshAsset candidate) {
        if (!IsOpen() || !kb::scene::ValidateSkeletalMeshAsset(candidate).valid) return false;
        if (historyCursor_ + 1U < history_.size()) history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(historyCursor_ + 1U), history_.end());
        history_.push_back(std::move(candidate));
        historyCursor_ = history_.size() - 1U;
        working_ = history_[historyCursor_];
        return true;
    }

    [[nodiscard]] bool Undo() {
        if (!CanUndo()) return false;
        --historyCursor_;
        working_ = history_[historyCursor_];
        return true;
    }

    [[nodiscard]] bool Redo() {
        if (!CanRedo()) return false;
        ++historyCursor_;
        working_ = history_[historyCursor_];
        return true;
    }

    [[nodiscard]] bool RevertToSaved() {
        if (!IsOpen()) return false;
        working_ = saved_;
        history_.clear();
        history_.push_back(*saved_);
        historyCursor_ = 0U;
        return true;
    }

    [[nodiscard]] bool ReplaceFromReimport(kb::scene::SkeletalMeshAsset candidate) {
        if (!IsOpen() || !kb::scene::ValidateSkeletalMeshAsset(candidate).valid) return false;
        saved_ = std::move(candidate);
        working_ = saved_;
        history_.clear();
        history_.push_back(*saved_);
        historyCursor_ = 0U;
        return true;
    }

    [[nodiscard]] bool MarkSaved() {
        if (!IsOpen() || !working_.has_value()) return false;
        saved_ = *working_;
        history_.clear();
        history_.push_back(*working_);
        historyCursor_ = 0U;
        return true;
    }

private:
    kb::assets::AssetId assetId_{};
    std::optional<kb::scene::SkeletalMeshAsset> saved_;
    std::optional<kb::scene::SkeletalMeshAsset> working_;
    std::vector<kb::scene::SkeletalMeshAsset> history_;
    std::size_t historyCursor_ = 0U;
};

} // namespace kb::editor
