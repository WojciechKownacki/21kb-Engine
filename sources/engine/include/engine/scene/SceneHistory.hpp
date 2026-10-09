#pragma once

#include "engine/scene/SceneEntity.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace kb::scene {

class Scene;

// Undo and Redo recreate the objects a command created or destroyed, so those come back as new entities.
struct SceneEntityRemap {
    SceneEntity from;
    SceneEntity to;
};

class SceneHistory {
public:
    explicit SceneHistory(Scene& scene) noexcept;

    // Opens a command. Every object written through the scene API until the command is committed keeps its
    // previous state once, so a command stores only the objects it changed.
    [[nodiscard]] bool Record(std::string label = {});
    // Closes the command Record opened; later writes are no longer part of it. Record, Undo and Redo do this too.
    void Commit();
    [[nodiscard]] bool CanUndo() const noexcept;
    [[nodiscard]] bool CanRedo() const noexcept;
    [[nodiscard]] bool Undo();
    [[nodiscard]] bool Redo();
    void Clear() noexcept;
    [[nodiscard]] std::size_t UndoCount() const noexcept;
    [[nodiscard]] std::size_t RedoCount() const noexcept;
    // Approximate memory the recorded commands hold.
    [[nodiscard]] std::size_t RecordedBytes() const noexcept;
    // The entities the last Undo or Redo recreated, each paired with the one it replaces; taking them clears them.
    [[nodiscard]] std::vector<SceneEntityRemap> TakeRecreatedEntities();
    // Keeps the recorded commands naming the same objects after something else recreated them.
    void RemapEntities(std::span<const SceneEntityRemap> remap);

private:
    Scene& scene_;
};

} // namespace kb::scene
