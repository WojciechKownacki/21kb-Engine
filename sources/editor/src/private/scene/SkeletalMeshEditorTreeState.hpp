#pragma once

#include "engine/scene/SkeletonAsset.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>
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
    std::uint64_t continuationMask = 0U;
    bool hasChildren = false;
    bool expanded = false;
    bool lastSibling = true;
    bool selected = false;
};

class SkeletalMeshEditorTreeState {
public:
    void SetSkeleton(const kb::scene::SkeletonAsset& skeleton) {
        const bool sameHierarchy = bones_.size() == skeleton.bones.size() &&
            std::ranges::equal(bones_, skeleton.bones, [](const auto& lhs, const auto& rhs) {
                return lhs.id == rhs.id && lhs.parentIndex == rhs.parentIndex && lhs.name == rhs.name;
            });
        bones_ = skeleton.bones;
        sockets_ = skeleton.sockets;
        if (!sameHierarchy) {
            expandedBones_.clear();
            expandedBones_.reserve(bones_.size());
            for (const kb::scene::SkeletonBone& bone : bones_) expandedBones_.insert(bone.id);
        } else {
            std::erase_if(expandedBones_, [this](kb::scene::SkeletonBoneId id) {
                return !ContainsBone(id);
            });
        }
        if (!ContainsBone(selectedBone_)) selectedBone_ = 0U;
        if (!ContainsSocket(selectedSocket_)) selectedSocket_.clear();
    }

    [[nodiscard]] bool SetFilter(std::string filter) {
        if (filter_ == filter) return false;
        filter_ = std::move(filter);
        scrollOffset_ = 0;
        return true;
    }

    [[nodiscard]] const std::string& Filter() const noexcept { return filter_; }
    [[nodiscard]] bool IsSearchFocused() const noexcept { return searchFocused_; }
    [[nodiscard]] kb::scene::SkeletonBoneId SelectedBone() const noexcept { return selectedBone_; }
    [[nodiscard]] const std::string& SelectedSocket() const noexcept { return selectedSocket_; }
    [[nodiscard]] int ScrollOffset() const noexcept { return scrollOffset_; }
    [[nodiscard]] bool IsScrollbarDragging() const noexcept { return scrollbarDragging_; }
    [[nodiscard]] bool IsExpanded(kb::scene::SkeletonBoneId boneId) const noexcept {
        return expandedBones_.contains(boneId);
    }

    [[nodiscard]] bool ToggleExpanded(kb::scene::SkeletonBoneId boneId) {
        if (!filter_.empty() || !BoneHasChildren(boneId)) return false;
        if (expandedBones_.erase(boneId) == 0U) {
            expandedBones_.insert(boneId);
        }
        scrollOffset_ = 0;
        return true;
    }

    [[nodiscard]] bool SetScrollOffset(int offset, int maxOffset) noexcept {
        const int clamped = std::clamp(offset, 0, std::max(0, maxOffset));
        if (scrollOffset_ == clamped) return false;
        scrollOffset_ = clamped;
        return true;
    }

    void BeginScrollbarDrag(int y) noexcept {
        scrollbarDragging_ = true;
        scrollbarDragY_ = y;
        scrollbarDragStartOffset_ = scrollOffset_;
    }

    void DragScrollbar(int y, int trackTravel, int maxOffset) noexcept {
        if (!scrollbarDragging_) return;
        const int offsetDelta = trackTravel <= 0 || maxOffset <= 0
            ? 0
            : ((y - scrollbarDragY_) * maxOffset) / trackTravel;
        static_cast<void>(SetScrollOffset(scrollbarDragStartOffset_ + offsetDelta, maxOffset));
    }

    void EndScrollbarDrag() noexcept { scrollbarDragging_ = false; }

    [[nodiscard]] bool SelectBone(kb::scene::SkeletonBoneId boneId) {
        if (!ContainsBone(boneId) || (selectedBone_ == boneId && selectedSocket_.empty())) return false;
        selectedBone_ = boneId;
        selectedSocket_.clear();
        ExpandAncestors(boneId);
        return true;
    }

    [[nodiscard]] bool SelectSocket(std::string socketName) {
        if (!ContainsSocket(socketName) || (selectedSocket_ == socketName && selectedBone_ == 0U)) return false;
        selectedBone_ = 0U;
        selectedSocket_ = std::move(socketName);
        const auto socket = std::ranges::find_if(sockets_, [this](const kb::scene::SkeletonSocket& value) {
            return value.name == selectedSocket_;
        });
        if (socket != sockets_.end()) {
            expandedBones_.insert(socket->boneId);
            ExpandAncestors(socket->boneId);
        }
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
        scrollOffset_ = 0;
    }

    void InsertSearchText(std::string_view text) {
        if (selectingAll_) {
            filter_.clear();
            selectingAll_ = false;
        }
        filter_.append(text);
        scrollOffset_ = 0;
    }

    void BackspaceSearch() {
        if (selectingAll_) {
            filter_.clear();
            selectingAll_ = false;
        } else if (!filter_.empty()) {
            filter_.pop_back();
        }
        scrollOffset_ = 0;
    }

    void SelectAllSearch() noexcept { selectingAll_ = true; }
    void ClearSearch() noexcept {
        filter_.clear();
        selectingAll_ = false;
        scrollOffset_ = 0;
    }

    [[nodiscard]] std::vector<SkeletalMeshEditorTreeRow> Rows() const {
        const std::string filter = Lower(filter_);
        std::vector<bool> visible(bones_.size(), filter.empty());
        std::vector<bool> visibleSockets(sockets_.size(), filter.empty());
        const auto revealBonePath = [this, &visible](std::size_t boneIndex) {
            std::size_t remaining = bones_.size();
            for (std::int32_t current = static_cast<std::int32_t>(boneIndex);
                current >= 0 && static_cast<std::size_t>(current) < bones_.size() && remaining > 0U;
                current = bones_[static_cast<std::size_t>(current)].parentIndex, --remaining) {
                visible[static_cast<std::size_t>(current)] = true;
            }
        };
        for (std::size_t index = 0U; index < bones_.size(); ++index) {
            if (!filter.empty() && Lower(bones_[index].name).find(filter) != std::string::npos) {
                revealBonePath(index);
            }
        }
        if (!filter.empty()) {
            for (std::size_t socketIndex = 0U; socketIndex < sockets_.size(); ++socketIndex) {
                if (Lower(sockets_[socketIndex].name).find(filter) == std::string::npos) continue;
                visibleSockets[socketIndex] = true;
                const auto owner = std::ranges::find_if(bones_, [this, socketIndex](const kb::scene::SkeletonBone& bone) {
                    return bone.id == sockets_[socketIndex].boneId;
                });
                if (owner != bones_.end()) {
                    revealBonePath(static_cast<std::size_t>(std::distance(bones_.begin(), owner)));
                }
            }
        }

        std::vector<std::vector<std::size_t>> boneChildren(bones_.size());
        std::vector<std::size_t> roots;
        roots.reserve(bones_.size());
        for (std::size_t index = 0U; index < bones_.size(); ++index) {
            const std::int32_t parent = bones_[index].parentIndex;
            if (parent >= 0 && static_cast<std::size_t>(parent) < bones_.size()) {
                boneChildren[static_cast<std::size_t>(parent)].push_back(index);
            } else {
                roots.push_back(index);
            }
        }
        std::vector<std::vector<std::size_t>> socketChildren(bones_.size());
        std::vector<bool> emittedSockets(sockets_.size(), false);
        std::vector<bool> ownedSockets(sockets_.size(), false);
        for (std::size_t socketIndex = 0U; socketIndex < sockets_.size(); ++socketIndex) {
            const auto owner = std::ranges::find_if(bones_, [this, socketIndex](const kb::scene::SkeletonBone& bone) {
                return bone.id == sockets_[socketIndex].boneId;
            });
            if (owner != bones_.end()) {
                socketChildren[static_cast<std::size_t>(std::distance(bones_.begin(), owner))].push_back(socketIndex);
                ownedSockets[socketIndex] = true;
            }
        }

        std::vector<SkeletalMeshEditorTreeRow> rows;
        rows.reserve(bones_.size() + sockets_.size());
        const bool filtering = !filter.empty();
        std::function<void(std::size_t, std::uint32_t, std::uint64_t, bool)> emitBone;
        emitBone = [&](std::size_t boneIndex, std::uint32_t depth, std::uint64_t continuationMask, bool lastSibling) {
            if (!visible[boneIndex]) return;
            const bool hasVisibleBoneChildren = std::ranges::any_of(
                boneChildren[boneIndex], [&visible](std::size_t child) { return visible[child]; });
            const bool hasVisibleSocketChildren = std::ranges::any_of(
                socketChildren[boneIndex], [&visibleSockets](std::size_t child) { return visibleSockets[child]; });
            const bool hasChildren = hasVisibleBoneChildren || hasVisibleSocketChildren;
            const bool expanded = filtering || expandedBones_.contains(bones_[boneIndex].id);
            rows.push_back(SkeletalMeshEditorTreeRow{
                .kind = SkeletalMeshEditorTreeItemKind::Bone,
                .boneId = bones_[boneIndex].id,
                .label = bones_[boneIndex].name,
                .depth = depth,
                .continuationMask = continuationMask,
                .hasChildren = hasChildren,
                .expanded = expanded,
                .lastSibling = lastSibling,
                .selected = selectedBone_ == bones_[boneIndex].id,
            });
            if (!hasChildren || !expanded) return;

            std::vector<std::pair<bool, std::size_t>> children;
            children.reserve(socketChildren[boneIndex].size() + boneChildren[boneIndex].size());
            for (const std::size_t socketIndex : socketChildren[boneIndex]) {
                if (visibleSockets[socketIndex]) children.emplace_back(true, socketIndex);
            }
            for (const std::size_t childIndex : boneChildren[boneIndex]) {
                if (visible[childIndex]) children.emplace_back(false, childIndex);
            }
            const std::uint64_t childContinuationMask = depth < 64U && !lastSibling
                ? continuationMask | (std::uint64_t{ 1U } << depth)
                : continuationMask;
            for (std::size_t childPosition = 0U; childPosition < children.size(); ++childPosition) {
                const bool childLast = childPosition + 1U == children.size();
                const auto [socket, childIndex] = children[childPosition];
                if (!socket) {
                    emitBone(childIndex, depth + 1U, childContinuationMask, childLast);
                    continue;
                }
                const kb::scene::SkeletonSocket& value = sockets_[childIndex];
                rows.push_back(SkeletalMeshEditorTreeRow{
                    .kind = SkeletalMeshEditorTreeItemKind::Socket,
                    .boneId = value.boneId,
                    .socketName = value.name,
                    .label = value.name,
                    .depth = depth + 1U,
                    .continuationMask = childContinuationMask,
                    .lastSibling = childLast,
                    .selected = selectedSocket_ == value.name,
                });
                emittedSockets[childIndex] = true;
            }
        };
        for (std::size_t rootPosition = 0U; rootPosition < roots.size(); ++rootPosition) {
            emitBone(roots[rootPosition], 0U, 0U, rootPosition + 1U == roots.size());
        }
        for (std::size_t socketIndex = 0U; socketIndex < sockets_.size(); ++socketIndex) {
            if (!visibleSockets[socketIndex] || ownedSockets[socketIndex] || emittedSockets[socketIndex]) continue;
            const kb::scene::SkeletonSocket& socket = sockets_[socketIndex];
            rows.push_back(SkeletalMeshEditorTreeRow{
                .kind = SkeletalMeshEditorTreeItemKind::Socket,
                .boneId = socket.boneId,
                .socketName = socket.name,
                .label = socket.name,
                .lastSibling = socketIndex + 1U == sockets_.size(),
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

    [[nodiscard]] bool BoneHasChildren(kb::scene::SkeletonBoneId boneId) const noexcept {
        const auto bone = std::ranges::find_if(bones_, [boneId](const kb::scene::SkeletonBone& value) {
            return value.id == boneId;
        });
        if (bone == bones_.end()) return false;
        const std::size_t index = static_cast<std::size_t>(std::distance(bones_.begin(), bone));
        return std::ranges::any_of(bones_, [index](const kb::scene::SkeletonBone& value) {
                   return value.parentIndex == static_cast<std::int32_t>(index);
               }) ||
            std::ranges::any_of(sockets_, [boneId](const kb::scene::SkeletonSocket& value) {
                return value.boneId == boneId;
            });
    }

    void ExpandAncestors(kb::scene::SkeletonBoneId boneId) {
        const auto bone = std::ranges::find_if(bones_, [boneId](const kb::scene::SkeletonBone& value) {
            return value.id == boneId;
        });
        if (bone == bones_.end()) return;
        std::int32_t parent = bone->parentIndex;
        std::size_t remaining = bones_.size();
        while (parent >= 0 && static_cast<std::size_t>(parent) < bones_.size() && remaining-- > 0U) {
            expandedBones_.insert(bones_[static_cast<std::size_t>(parent)].id);
            parent = bones_[static_cast<std::size_t>(parent)].parentIndex;
        }
    }

    std::vector<kb::scene::SkeletonBone> bones_;
    std::vector<kb::scene::SkeletonSocket> sockets_;
    std::unordered_set<kb::scene::SkeletonBoneId> expandedBones_;
    std::string filter_;
    kb::scene::SkeletonBoneId selectedBone_ = 0U;
    std::string selectedSocket_;
    int scrollOffset_ = 0;
    int scrollbarDragY_ = 0;
    int scrollbarDragStartOffset_ = 0;
    bool searchFocused_ = false;
    bool selectingAll_ = false;
    bool scrollbarDragging_ = false;
};

} // namespace kb::editor
