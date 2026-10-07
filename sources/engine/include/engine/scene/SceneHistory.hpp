#pragma once

#include "engine/scene/SceneEntity.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace kb::scene {

class Scene;

// Undo and Redo rebuild the scene from a snapshot, so each object comes back as a new entity.
struct SceneEntityRemap {
    SceneEntity from;
    SceneEntity to;
};

class SceneHistory {
public:
    explicit SceneHistory(Scene& scene) noexcept;

    [[nodiscard]] bool Record(std::string label = {});
    [[nodiscard]] bool CanUndo() const noexcept;
    [[nodiscard]] bool CanRedo() const noexcept;
    [[nodiscard]] bool Undo();
    [[nodiscard]] bool Redo();
    void Clear() noexcept;
    [[nodiscard]] std::size_t UndoCount() const noexcept;
    [[nodiscard]] std::size_t RedoCount() const noexcept;
    // The entities the last Undo or Redo recreated, each paired with the one it replaces; taking them clears them.
    [[nodiscard]] std::vector<SceneEntityRemap> TakeRecreatedEntities();
    // Keeps the recorded snapshots naming the same objects after something else recreated them.
    void RemapEntities(std::span<const SceneEntityRemap> remap);

private:
    Scene& scene_;
};

} // namespace kb::scene
