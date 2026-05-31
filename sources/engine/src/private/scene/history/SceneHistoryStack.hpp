#pragma once

#include "engine/scene/ScenePrefab.hpp"

#include <string>
#include <vector>

namespace kb::scene {

struct SceneHistoryEntry {
    std::string label;
    std::vector<ScenePrefab> roots;
};

class SceneHistoryStack {
public:
    [[nodiscard]] bool Empty() const noexcept;
    [[nodiscard]] std::size_t Size() const noexcept;
    void Push(SceneHistoryEntry entry);
    [[nodiscard]] SceneHistoryEntry Pop();
    void Clear() noexcept;

private:
    std::vector<SceneHistoryEntry> entries_;
};

} // namespace kb::scene
