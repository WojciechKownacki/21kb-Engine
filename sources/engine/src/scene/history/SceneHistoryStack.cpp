#include "scene/history/SceneHistoryStack.hpp"

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

} // namespace kb::scene
