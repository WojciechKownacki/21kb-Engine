#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/scene/TimelineAssetIO.hpp"

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace kb::editor {

// Typed working copies share validation with the runtime loader. Invalid edits
// never enter history or replace a preview's last valid document.
class TimelineEditorDocumentState {
public:
    [[nodiscard]] bool Open(kb::assets::AssetId id, kb::scene::TimelineAsset asset) {
        if (!id.IsValid() || !kb::scene::TimelineAssetIO::Validate(asset)) return false;
        assetId_ = id;
        history_.assign(1, std::move(asset));
        cursor_ = 0;
        savedCursor_ = 0;
        groupActive_ = false;
        groupHasEdit_ = false;
        return true;
    }

    [[nodiscard]] kb::assets::AssetId AssetId() const noexcept { return assetId_; }
    [[nodiscard]] const kb::scene::TimelineAsset* WorkingCopy() const noexcept {
        return cursor_ < history_.size() ? &history_[cursor_] : nullptr;
    }
    [[nodiscard]] bool Dirty() const noexcept {
        return WorkingCopy() != nullptr && (!savedCursor_ || *savedCursor_ != cursor_);
    }
    [[nodiscard]] bool CanUndo() const noexcept { return cursor_ > 0; }
    [[nodiscard]] bool CanRedo() const noexcept { return cursor_ + 1 < history_.size(); }
    void BeginGroup() noexcept { groupActive_ = true; groupHasEdit_ = false; }
    void EndGroup() noexcept { groupActive_ = false; groupHasEdit_ = false; }
    [[nodiscard]] bool Undo() noexcept {
        EndGroup();
        if (!CanUndo()) return false;
        --cursor_;
        return true;
    }
    [[nodiscard]] bool Redo() noexcept {
        EndGroup();
        if (!CanRedo()) return false;
        ++cursor_;
        return true;
    }
    void MarkSaved() noexcept {
        EndGroup();
        if (WorkingCopy()) savedCursor_ = cursor_;
    }

    template<class Edit>
    [[nodiscard]] bool Apply(Edit&& edit) {
        const auto* current = WorkingCopy();
        if (current == nullptr) return false;
        auto candidate = *current;
        if (!edit(candidate) || !kb::scene::TimelineAssetIO::Validate(candidate)) return false;
        if (groupActive_ && groupHasEdit_) {
            if (savedCursor_ && *savedCursor_ == cursor_) savedCursor_.reset();
            history_[cursor_] = std::move(candidate);
            return true;
        }
        if (savedCursor_ && *savedCursor_ > cursor_) savedCursor_.reset();
        history_.resize(cursor_ + 1);
        history_.push_back(std::move(candidate));
        ++cursor_;
        groupHasEdit_ = groupActive_;
        constexpr std::size_t kMaxHistory = 128;
        if (history_.size() > kMaxHistory) {
            history_.erase(history_.begin());
            --cursor_;
            if (savedCursor_) {
                if (*savedCursor_ == 0) savedCursor_.reset();
                else --*savedCursor_;
            }
        }
        return true;
    }

private:
    kb::assets::AssetId assetId_{};
    std::vector<kb::scene::TimelineAsset> history_;
    std::size_t cursor_ = 0;
    std::optional<std::size_t> savedCursor_;
    bool groupActive_ = false;
    bool groupHasEdit_ = false;
};

} // namespace kb::editor
