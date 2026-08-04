#pragma once

#include "engine/scene/SkeletonAsset.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace kb::editor {

enum class SkeletalMeshEditorTreeItemKind : std::uint8_t {
    Bone,
    Socket,
};

struct SkeletalMeshEditorTreeRow {
    SkeletalMeshEditorTreeItemKind kind = SkeletalMeshEditorTreeItemKind::Bone;
    kb::scene::SkeletonBoneId boneId = 0U;
    std::string socketName;
    std::string label;
    std::uint32_t depth = 0U;
    bool selected = false;
};

class SkeletalMeshEditorTreeState {
public:
    void SetSkeleton(const kb::scene::SkeletonAsset& skeleton) {
        bones_ = skeleton.bones;
        sockets_ = skeleton.sockets;
        if (!ContainsBone(selectedBone_)) selectedBone_ = 0U;
        if (!ContainsSocket(selectedSocket_)) selectedSocket_.clear();
    }

    [[nodiscard]] bool SetFilter(std::string filter) {
        if (filter_ == filter) return false;
        filter_ = std::move(filter);
        return true;
    }

    [[nodiscard]] const std::string& Filter() const noexcept { return filter_; }
    [[nodiscard]] bool IsSearchFocused() const noexcept { return searchFocused_; }
    [[nodiscard]] kb::scene::SkeletonBoneId SelectedBone() const noexcept { return selectedBone_; }
    [[nodiscard]] const std::string& SelectedSocket() const noexcept { return selectedSocket_; }

    [[nodiscard]] bool SelectBone(kb::scene::SkeletonBoneId boneId) {
        if (!ContainsBone(boneId) || (selectedBone_ == boneId && selectedSocket_.empty())) return false;
        selectedBone_ = boneId;
        selectedSocket_.clear();
        return true;
    }

    [[nodiscard]] bool SelectSocket(std::string socketName) {
        if (!ContainsSocket(socketName) || (selectedSocket_ == socketName && selectedBone_ == 0U)) return false;
        selectedBone_ = 0U;
        selectedSocket_ = std::move(socketName);
        return true;
    }

    [[nodiscard]] bool ClearSelection() {
        if (selectedBone_ == 0U && selectedSocket_.empty()) return false;
        selectedBone_ = 0U;
        selectedSocket_.clear();
        return true;
    }

    void FocusSearch(bool focused) noexcept {
        searchFocused_ = focused;
        if (!focused) selectingAll_ = false;
    }

    void AppendSearchText(wchar_t character) {
        if (character < 0x20 || character > 0x7FU) return;
        if (selectingAll_) {
            filter_.clear();
            selectingAll_ = false;
        }
        filter_.push_back(static_cast<char>(character));
    }

    void InsertSearchText(std::string_view text) {
        if (selectingAll_) {
            filter_.clear();
            selectingAll_ = false;
        }
        filter_.append(text);
    }

    void BackspaceSearch() {
        if (selectingAll_) {
            filter_.clear();
            selectingAll_ = false;
        } else if (!filter_.empty()) {
            filter_.pop_back();
        }
    }

    void SelectAllSearch() noexcept { selectingAll_ = true; }
    void ClearSearch() noexcept {
        filter_.clear();
        selectingAll_ = false;
    }

    [[nodiscard]] std::vector<SkeletalMeshEditorTreeRow> Rows() const {
        const std::string filter = Lower(filter_);
        std::vector<bool> visible(bones_.size(), filter.empty());
        for (std::size_t index = 0U; index < bones_.size(); ++index) {
            if (!filter.empty() && Lower(bones_[index].name).find(filter) != std::string::npos) {
                for (std::int32_t parent = static_cast<std::int32_t>(index); parent >= 0;
                    parent = bones_[static_cast<std::size_t>(parent)].parentIndex) {
                    visible[static_cast<std::size_t>(parent)] = true;
                }
            }
        }

        std::vector<SkeletalMeshEditorTreeRow> rows;
        rows.reserve(bones_.size() + sockets_.size());
        for (std::size_t index = 0U; index < bones_.size(); ++index) {
            if (!visible[index]) continue;
            std::uint32_t depth = 0U;
            for (std::int32_t parent = bones_[index].parentIndex; parent >= 0;
                parent = bones_[static_cast<std::size_t>(parent)].parentIndex) {
                ++depth;
            }
            rows.push_back(SkeletalMeshEditorTreeRow{
                .kind = SkeletalMeshEditorTreeItemKind::Bone,
                .boneId = bones_[index].id,
                .label = bones_[index].name,
                .depth = depth,
                .selected = selectedBone_ == bones_[index].id,
            });
        }
        for (const kb::scene::SkeletonSocket& socket : sockets_) {
            if (!filter.empty() && Lower(socket.name).find(filter) == std::string::npos) continue;
            rows.push_back(SkeletalMeshEditorTreeRow{
                .kind = SkeletalMeshEditorTreeItemKind::Socket,
                .boneId = socket.boneId,
                .socketName = socket.name,
                .label = socket.name,
                .selected = selectedSocket_ == socket.name,
            });
        }
        return rows;
    }

private:
    [[nodiscard]] static std::string Lower(std::string value) {
        std::ranges::transform(value, value.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        return value;
    }

    [[nodiscard]] bool ContainsBone(kb::scene::SkeletonBoneId boneId) const noexcept {
        return boneId != 0U && std::ranges::any_of(bones_, [boneId](const kb::scene::SkeletonBone& bone) {
            return bone.id == boneId;
        });
    }

    [[nodiscard]] bool ContainsSocket(std::string_view name) const noexcept {
        return !name.empty() && std::ranges::any_of(sockets_, [name](const kb::scene::SkeletonSocket& socket) {
            return socket.name == name;
        });
    }

    std::vector<kb::scene::SkeletonBone> bones_;
    std::vector<kb::scene::SkeletonSocket> sockets_;
    std::string filter_;
    kb::scene::SkeletonBoneId selectedBone_ = 0U;
    std::string selectedSocket_;
    bool searchFocused_ = false;
    bool selectingAll_ = false;
};

} // namespace kb::editor
