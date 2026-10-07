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
    [[nodiscard]] static bool CanUndo(const Scene& scene) noexcept;
    [[nodiscard]] static bool CanRedo(const Scene& scene) noexcept;
    [[nodiscard]] static bool Undo(Scene& scene);
    [[nodiscard]] static bool Redo(Scene& scene);
    static void Clear(Scene& scene) noexcept;
    [[nodiscard]] static std::size_t UndoCount(const Scene& scene) noexcept;
    [[nodiscard]] static std::size_t RedoCount(const Scene& scene) noexcept;
    [[nodiscard]] static std::vector<SceneEntityRemap> TakeRecreatedEntities(Scene& scene) noexcept;
    static void RemapEntities(Scene& scene, std::span<const SceneEntityRemap> remap);
};

} // namespace kb::scene
