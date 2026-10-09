#pragma once

#include "engine/scene/SceneHistory.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace kb::scene {

class Scene;

class SceneHistoryService {
public:
    SceneHistoryService() = delete;

    [[nodiscard]] static bool Record(Scene& scene, std::string label);
    static void Commit(Scene& scene);
    [[nodiscard]] static bool CanUndo(const Scene& scene) noexcept;
    [[nodiscard]] static bool CanRedo(const Scene& scene) noexcept;
    [[nodiscard]] static bool Undo(Scene& scene);
    [[nodiscard]] static bool Redo(Scene& scene);
    static void Clear(Scene& scene) noexcept;
    [[nodiscard]] static std::size_t UndoCount(const Scene& scene) noexcept;
    [[nodiscard]] static std::size_t RedoCount(const Scene& scene) noexcept;
    [[nodiscard]] static std::size_t RecordedBytes(const Scene& scene) noexcept;
    [[nodiscard]] static std::vector<SceneEntityRemap> TakeRecreatedEntities(Scene& scene) noexcept;
    static void RemapEntities(Scene& scene, std::span<const SceneEntityRemap> remap);

    // Called by the scene API right before it writes an object's name, flags, transform, visibility or components
    // (or hands out a pointer to write through). While a command records, the object's state is kept once.
    static void NoteObjectChanging(Scene& scene, SceneEntity entity) noexcept;
    // Right before `entity` moves under `newParent` (invalid: to the roots).
    static void NoteObjectMoving(Scene& scene, SceneEntity entity, SceneEntity newParent) noexcept;
    // Right before `root` and its subtree are destroyed.
    static void NoteSubtreeDestroying(Scene& scene, SceneEntity root) noexcept;
    // Right after entities are created, before anything is written to them.
    static void NoteObjectsCreated(Scene& scene, std::span<const SceneEntity> entities) noexcept;
};

} // namespace kb::scene
