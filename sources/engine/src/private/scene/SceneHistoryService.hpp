#pragma once

#include <cstddef>
#include <string>

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
};

} // namespace kb::scene
