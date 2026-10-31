#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/scene/BlendSpace2D.hpp"

#include <optional>
#include <utility>
#include <vector>

namespace kb::editor {

class BlendSpace2DEditorDocumentState final {
public:
    [[nodiscard]] bool Open(kb::assets::AssetId id, kb::scene::BlendSpace2DAsset asset, std::string& error) {
        error.clear();
        if (!id.IsValid()) { error="BlendSpace2D asset identity is invalid";return false; }
        auto plan = kb::scene::BlendSpace2DPlan::Build(asset,error);
        if (!plan) return false;
        id_ = id;
        history_.clear();
        history_.push_back({std::move(asset),std::move(*plan)});
        cursor_ = 0;
        saved_ = 0;
        EndGroup();
        return true;
    }
    [[nodiscard]] kb::assets::AssetId AssetId() const noexcept { return id_; }
    [[nodiscard]] const kb::scene::BlendSpace2DAsset* WorkingCopy() const noexcept {
        return cursor_ < history_.size() ? &history_[cursor_].asset : nullptr;
    }
    [[nodiscard]] const kb::scene::BlendSpace2DPlan* Plan() const noexcept {
        return cursor_ < history_.size() ? &history_[cursor_].plan : nullptr;
    }
    [[nodiscard]] bool Dirty() const noexcept { return WorkingCopy() && (!saved_ || *saved_ != cursor_); }
    [[nodiscard]] bool CanUndo() const noexcept { return cursor_ > 0; }
    [[nodiscard]] bool CanRedo() const noexcept { return cursor_ + 1 < history_.size(); }
    void BeginGroup() noexcept { group_ = true; edited_ = false; }
    void EndGroup() noexcept { group_ = edited_ = false; }
    void MarkSaved() noexcept { EndGroup(); if (WorkingCopy()) saved_ = cursor_; }
    [[nodiscard]] bool Undo() noexcept { EndGroup(); if (!CanUndo()) return false; --cursor_; return true; }
    [[nodiscard]] bool Redo() noexcept { EndGroup(); if (!CanRedo()) return false; ++cursor_; return true; }

    template<class Edit>
    [[nodiscard]] bool Apply(Edit&& edit, std::string& error) {
        error.clear();
        if (!WorkingCopy()) { error = "Open a BlendSpace2D asset first"; return false; }
        auto asset = *WorkingCopy();
        if (!edit(asset)) return false;
        for (const auto& sample : asset.samples) {
            if (sample.clipReference.size() > 4096 || sample.clipReference.find_first_of("\r\n") != std::string::npos ||
                sample.clipReference.find('\0') != std::string::npos) {
                error = "Invalid BlendSpace2D clip reference"; return false;
            }
        }
        auto plan = kb::scene::BlendSpace2DPlan::Build(asset,error);
        if (!plan) return false;
        Entry candidate{std::move(asset),std::move(*plan)};
        if (group_ && edited_) {
            if (saved_ && *saved_ == cursor_) saved_.reset();
            history_[cursor_] = std::move(candidate);
            return true;
        }
        if (saved_ && *saved_ > cursor_) saved_.reset();
        history_.resize(cursor_ + 1);
        history_.push_back(std::move(candidate));
        ++cursor_;
        edited_ = group_;
        if (history_.size() > 128) {
            history_.erase(history_.begin()); --cursor_;
            if (saved_) { if (*saved_ == 0) saved_.reset(); else --*saved_; }
        }
        return true;
    }

    [[nodiscard]] bool Move(std::size_t index, float x, float y, std::string& error) {
        return Apply([&](auto& asset) {
            if (index >= asset.samples.size()) return false;
            auto& sample = asset.samples[index];
            if (sample.x == x && sample.y == y) return false;
            sample.x = x; sample.y = y; return true;
        },error);
    }

private:
    struct Entry { kb::scene::BlendSpace2DAsset asset; kb::scene::BlendSpace2DPlan plan; };
    kb::assets::AssetId id_{};
    std::vector<Entry> history_;
    std::size_t cursor_ = 0;
    std::optional<std::size_t> saved_;
    bool group_ = false, edited_ = false;
};
} // namespace kb::editor
