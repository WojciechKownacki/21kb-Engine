#include "scene/history/SceneHistoryStack.hpp"

#include <algorithm>
#include <utility>

namespace kb::scene {

bool SceneHistoryStack::Empty() const noexcept {
    return entries_.empty();
}

std::size_t SceneHistoryStack::Size() const noexcept {
    return entries_.size();
}

void SceneHistoryStack::Push(SceneHistoryEntry entry) {
    entries_.push_back(std::move(entry));
}

SceneHistoryEntry SceneHistoryStack::Pop() {
    SceneHistoryEntry entry = std::move(entries_.back());
    entries_.pop_back();
    return entry;
}

void SceneHistoryStack::Clear() noexcept {
    entries_.clear();
}

void SceneHistoryStack::RemapEntities(std::span<const SceneEntityRemap> remap) noexcept {
    for (SceneHistoryEntry& entry : entries_) {
        for (SceneEntity& entity : entry.entities) {
            const auto replacement = std::ranges::lower_bound(remap, entity.Id(), {}, [](const SceneEntityRemap& candidate) noexcept {
                return candidate.from.Id();
            });
            if (replacement != remap.end() && replacement->from == entity) {
                entity = replacement->to;
            }
        }
    }
}

} // namespace kb::scene
